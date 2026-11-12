// SPDX-License-Identifier: GPL-2.0-or-later
//
// OBS-wide performance statistics, collected with exactly the same public
// APIs as OBS's native Stats dock (frontend/widgets/OBSBasicStats.cpp).
// These are global and are never attributed to individual outputs.
//
// Rendering/encoding-lag counting window (1.0.1), aligned with the native dock:
//   - created at FINISHED_LOADING -> baseline now. OBS takes its own baseline
//     (OBSBasicStats::InitializeValues) in the same synchronous OBSInit()
//     sequence just before emitting FINISHED_LOADING, so both windows start at
//     the same point (startup hiccups while loading are excluded by both);
//   - libobs "video_reset" signal (public, obs_get_signal_handler) -> baseline
//     again, like OBSBasic::ResetVideo() -> InitializeValues();
//   - resetBaselines() -> this dock's Reset button;
//   - counters going backwards -> re-baseline (FrameWindow).
#pragma once

#include <core/frame-window.hpp>
#include <core/snapshot.hpp>

#include <callback/calldata.h>
#include <util/platform.h>

#include <mutex>
#include <optional>

namespace msd {

class SystemStats {
public:
	SystemStats();  // takes the "since loaded" baseline, connects video_reset
	~SystemStats(); // disconnects video_reset (waits for an in-flight callback)

	SystemStats(const SystemStats &) = delete;
	SystemStats &operator=(const SystemStats &) = delete;

	SystemSnapshot sample(uint64_t nowNs);
	void resetBaselines(); // this dock's Reset

private:
	struct Counters {
		uint32_t rendered = 0;
		uint32_t lagged = 0;
		bool haveVideo = false;
		uint32_t encoded = 0;
		uint32_t skipped = 0;
	};
	static Counters readCounters();
	static void onVideoReset(void *data, calldata_t *cd);
	void applyBaseline(const Counters &c, LagWindow window);

	os_cpu_usage_info_t *cpu_ = nullptr;
	uint64_t lastDiskQueryNs_ = 0;
	Metric<uint64_t> disk_ = Metric<uint64_t>::na(Unavailable::NotMeasured);

	FrameWindow render_;
	FrameWindow encode_;
	LagWindow window_ = LagWindow::SinceLoaded;

	// Counters captured by the video_reset callback (any thread), applied by
	// the next sample() on the UI thread.
	std::mutex pendingMutex_;
	std::optional<Counters> pendingReset_;
	bool signalConnected_ = false;
};

} // namespace msd
