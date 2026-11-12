// SPDX-License-Identifier: GPL-2.0-or-later
//
// StatsCollector owns discovery, per-output trackers, the session registry
// and OBS-wide statistics. It lives on the UI thread.
//
// Timers:
//  * discovery (250 ms): libobs has no "output created" signal, so new
//    outputs are found by walking obs_enum_outputs. Only outputs not yet
//    known are touched (weak refs only inside the enumeration callback).
//    The short interval keeps the window in which a fast-connecting output
//    could start before its signals are connected small.
//  * statistics (1000 ms): samples tracked outputs, updates the registry and,
//    while the dock is visible, OBS-wide stats + publishes a snapshot.
//    Sampling continues while hidden so session counters stay correct.
#pragma once

#include "aitum-bridge.hpp"
#include "output-tracker.hpp"
#include "system-stats.hpp"

#include <core/session-registry.hpp>
#include <core/snapshot.hpp>

#include <obs-frontend-api.h>

#include <QObject>
#include <QTimer>

#include <memory>
#include <unordered_set>
#include <vector>

namespace msd {

class StatsCollector : public QObject {
	Q_OBJECT

public:
	explicit StatsCollector(QObject *parent = nullptr);
	~StatsCollector() override;

	void start();    // after OBS_FRONTEND_EVENT_FINISHED_LOADING
	void shutdown(); // on OBS_FRONTEND_EVENT_EXIT / module unload; idempotent
	bool isShutdown() const { return shutdown_; }

	void setDisplayActive(bool active);
	void handleFrontendEvent(enum obs_frontend_event event);

	void resetStatistics();
	void clearEnded();

	const DockSnapshot &lastSnapshot() const { return last_; }

signals:
	void snapshotReady(const msd::DockSnapshot &snapshot);

private:
	void onDiscoveryTimer();
	void onStatsTimer();
	void discover();
	void pruneDeadTrackers();
	void pruneIgnored();
	void refreshAitumConfig(uint64_t nowNs, bool force);
	void publish(uint64_t nowNs);
	void logTick(const std::vector<RowSnapshot> &all, const DockSnapshot &snap) const;

	std::vector<std::unique_ptr<OutputTracker>> trackers_;
	// Non-streaming outputs (recording, replay buffer, virtual camera, ...)
	// are remembered by weak ref so they are not re-inspected every scan.
	struct Ignored {
		obs_weak_output_t *weak;
		const void *identity; // never dereferenced
	};
	std::vector<Ignored> ignored_;
	std::vector<VanishedOutput> vanished_;

	SessionRegistry registry_;
	std::unique_ptr<SystemStats> system_;
	AitumBridge aitum_;

	QTimer discoveryTimer_;
	QTimer statsTimer_;

	bool started_ = false;
	bool shutdown_ = false;
	bool displayActive_ = false;
	bool firstScanDone_ = false;
	bool aitumConfigKnown_ = false;
	uint64_t nextInstanceId_ = 1;
	uint64_t lastAitumQueryNs_ = 0;
	size_t lastAitumTrackerCount_ = 0;
	DockSnapshot last_;

	bool debugTicks_ = false;
	uint64_t lastCollectNs_ = 0;
	uint64_t uiNsTotal_ = 0;
	uint64_t uiNsMax_ = 0;
	uint64_t uiCount_ = 0;
	uint64_t tickCount_ = 0;
	uint64_t tickNsTotal_ = 0;
	uint64_t tickNsMax_ = 0;
};

} // namespace msd
