// SPDX-License-Identifier: GPL-2.0-or-later
//
// SessionRegistry turns per-tick observations of libobs output objects into a
// stable list of destination rows:
//  * one row per destination; each output object maps to exactly one row
//    (no double counting), identical names are disambiguated ("#2");
//  * a restarted destination (Aitum creates a new object per start) re-uses
//    its row and starts a fresh session;
//  * finished sessions are retained with their final statistics;
//  * configured-but-stopped destinations (from Aitum's vendor API) get rows.
#pragma once

#include "bitrate-meter.hpp"
#include "observation.hpp"
#include "snapshot.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace msd {

class SessionRegistry {
public:
	static constexpr size_t kMaxRetainedEndedRows = 16;
	// Consecutive inactive polls before a live session without a "stop"
	// signal is considered ended (fallback only; normally the signal wins).
	static constexpr int kInactiveGraceTicks = 2;
	// "Connecting" inferred from Aitum's create-on-start behaviour (no signal
	// seen) expires after this long without activity: the object evidently
	// is not connecting, so the row falls back to "not started".
	static constexpr uint64_t kInferredConnectTimeoutNs = 30'000'000'000ULL;

	SessionRegistry();

	void update(uint64_t nowNs, const std::vector<OutputObservation> &live,
		    const std::vector<VanishedOutput> &vanished);

	// Replace the configured destination list of a provider (e.g. Aitum).
	void setConfiguredDestinations(ProviderKind kind, const std::vector<ConfiguredDestination> &dests);
	// Provider configuration is no longer known (e.g. bridge unavailable):
	// stop pruning by configuration.
	void forgetConfiguredDestinations(ProviderKind kind);

	std::vector<RowSnapshot> snapshot(uint64_t nowNs) const;

	// Like OBS's native "Reset": dropped/total frame counters restart at 0.
	void resetStatistics();
	// Remove finished rows that are not configured; reset configured rows.
	void clearEnded();

	size_t rowCount() const { return rows_.size(); }

private:
	struct Session {
		bool exists = false;
		OutputState state = OutputState::Idle;
		StartSource startSource = StartSource::None;
		uint64_t beginNs = 0;
		uint64_t startNs = 0;
		uint64_t endNs = 0;
		uint64_t lastConnectedNs = 0;
		int reconnects = 0;
		int stopCode = 0;
		std::string lastError;
		bool endReasonUnknown = false;
		bool endedBySignal = false;
		bool inferredConnecting = false;
		int inactiveTicks = 0;
		CounterAccumulator bytes;
		CounterAccumulator dropped;
		CounterAccumulator total;
		BitrateMeter meter;
		CongestionSmoother congestion;
		RecentDropWindow recent;
		uint64_t droppedBaseline = 0;
		uint64_t totalBaseline = 0;
		bool sinceReset = false;
		bool sawDrops = false;
		bool sawCongestion = false;
	};

	struct Row {
		std::string key;
		std::string baseKey;
		int ordinal = 1;
		ProviderKind kind = ProviderKind::Generic;
		std::string destination;
		std::string outputName;
		std::string typeId;
		std::string serviceName;
		std::string ingestHost;
		bool vertical = false;
		bool configured = false;
		int configIndex = -1;
		uint64_t order = 0;
		uint64_t liveInstance = 0;
		bool signalsObserved = false; // any signal seen for liveInstance
		bool haveSample = false;
		OutputSample lastSample;
		OutputCapabilities caps;
		Session session;
	};

	struct Binding {
		std::string key;
		std::string baseKey;
	};

	Row *find(const std::string &key);
	const Row *find(const std::string &key) const;
	Row &ensureRow(const std::string &key, const std::string &baseKey, int ordinal, ProviderKind kind);
	Row *bind(const OutputObservation &obs);
	void detachInstance(uint64_t instanceId, uint64_t nowNs);

	void beginSession(Row &row, uint64_t ts, StartSource source);
	void endSession(Row &row, uint64_t ts, int code, const std::string &err, bool unknownReason, bool bySignal);
	void applyEvent(Row &row, const OutputEvent &e);
	void reconcile(Row &row, const OutputObservation &obs, uint64_t nowNs);
	void absorbCounters(Row &row, uint64_t ts, uint64_t bytes, int dropped, int total);
	void prune();

	RowSnapshot makeSnapshot(const Row &row, uint64_t nowNs) const;

	std::vector<std::unique_ptr<Row>> rows_;
	std::unordered_map<uint64_t, Binding> bindings_;
	uint64_t nextOrder_ = 0;
	bool aitumConfigKnown_ = false;
};

} // namespace msd
