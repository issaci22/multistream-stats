// SPDX-License-Identifier: GPL-2.0-or-later
//
// Display-ready values produced by the core for the UI. Every value that can
// be unavailable is a Metric<T>; the UI renders Metric::na as "N/A" with the
// reason as tooltip and never substitutes 0.
#pragma once

#include "metric.hpp"
#include "observation.hpp"
#include "status-model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace msd {

// Where the session's start time came from.
enum class StartSource {
	None,     // not live yet
	Signal,   // exact: "start" signal observed
	Detected, // polled: output found live within one discovery interval
	Unknown,  // output was already live when monitoring began
};

// What the OBS-wide rendering/encoding-lag counts are measured since.
enum class LagWindow {
	SinceLoaded,     // OBS finished loading (native Stats dock: InitializeValues)
	SinceVideoReset, // video settings were (re)applied (native: ResetVideo)
	SinceUserReset,  // this dock's Reset (native dock keeps its own window)
};

struct RowSnapshot {
	std::string key;
	ProviderKind kind = ProviderKind::Generic;
	std::string destination; // Aitum destination name; empty otherwise
	std::string outputName;
	std::string typeId;
	std::string serviceName;
	std::string ingestHost;
	int ordinal = 1;        // 1 = first of identical base keys
	bool vertical = false;
	bool configured = false;  // listed in the provider's configuration
	bool objectPresent = false; // a libobs output object currently exists
	bool everSampled = false;

	OutputState state = OutputState::Idle;
	Health health = Health::Inactive;
	bool hasSession = false;
	int stopCode = 0;
	std::string lastError;
	bool endReasonUnknown = false;
	uint64_t endNs = 0; // when the last session ended (0 = running / never ran)

	StartSource startSource = StartSource::None;
	Metric<uint64_t> durationNs;
	Metric<uint64_t> lastConnectedAgoNs;
	int reconnects = 0;

	Metric<double> kbps;
	Metric<double> avgKbps;
	Metric<uint64_t> bytes;
	Metric<uint64_t> dropped;
	Metric<uint64_t> totalFrames;
	Metric<double> dropPct;
	Metric<double> recentDropPct;
	Health dropHealth = Health::Inactive;
	Metric<double> congestion;
	Health congestionHealth = Health::Inactive;
	Metric<int> connectTimeMs;
	bool countsSinceReset = false;

	Metric<uint32_t> width;
	Metric<uint32_t> height;
	Metric<double> fps;
	EncoderInfo encoder;
	Metric<double> pipelineSkipPct;
	Health pipelineHealth = Health::Inactive;
	uint32_t delaySec = 0;
};

struct SystemSnapshot {
	Metric<double> cpuPercent;
	Metric<uint64_t> memoryBytes;
	Metric<uint64_t> diskFreeBytes;
	Metric<double> fps;
	double targetFps = 0.0;
	Health fpsHealth = Health::Inactive;
	Metric<double> renderMs;
	Health renderHealth = Health::Inactive;
	Metric<uint64_t> renderLagged;
	Metric<uint64_t> renderTotal;
	Metric<double> renderLagPct;
	Health renderLagHealth = Health::Inactive;
	Metric<uint64_t> encodeSkipped;
	Metric<uint64_t> encodeTotal;
	Metric<double> encodeLagPct;
	Health encodeLagHealth = Health::Inactive;
	// Period the rendering/encoding-lag counts cover (same rules as the native
	// Stats dock, see core/frame-window.hpp).
	LagWindow lagWindow = LagWindow::SinceLoaded;
};

struct SummarySnapshot {
	int rows = 0;
	int running = 0; // connecting + live + reconnecting + stopping
	int live = 0;
	int connecting = 0;
	int reconnecting = 0;
	int failed = 0; // ended Disconnected / Error, not yet restarted or cleared
	int warnings = 0;
	int problems = 0;
	Metric<double> sumKbps; // sum of measured per-output bitrates
	int unmeasured = 0;     // live outputs without a bitrate measurement yet
	Metric<uint64_t> droppedSum;
	Metric<uint64_t> framesSum;
	Metric<double> dropPct;
	int dropUnsupported = 0; // running outputs whose type doesn't report drops
	Health overall = Health::Inactive;
	Health systemHealth = Health::Inactive;
};

struct DockSnapshot {
	uint64_t timestampNs = 0;
	std::vector<RowSnapshot> rows;
	SummarySnapshot summary;
	SystemSnapshot system;
	// Most recent failure of an output that is no longer listed (see
	// core/aggregate.hpp lastFailure); hasLastFailure = false if none.
	bool hasLastFailure = false;
	RowSnapshot lastFailure;
	bool aitumLoaded = false;          // aitum-multistream module present
	bool aitumConfigAvailable = false; // websocket vendor query succeeded
};

} // namespace msd
