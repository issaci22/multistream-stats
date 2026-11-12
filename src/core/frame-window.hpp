// SPDX-License-Identifier: GPL-2.0-or-later
//
// Counting window for a pair of libobs frame counters ("affected / total"),
// e.g. rendering lag (obs_get_lagged_frames / obs_get_total_frames) or
// encoding lag (video_output_get_skipped_frames / _total_frames).
//
// Mirrors OBS 33's native Stats dock (frontend/widgets/OBSBasicStats.cpp):
//   - baseline(): OBSBasicStats::InitializeValues() - counting starts now
//     (OBS calls it right before FINISHED_LOADING and after every video reset);
//   - invalidate(): OBSBasicStats::Reset() - the next sample becomes the
//     baseline (first_* = 0xFFFFFFFF);
//   - a counter that goes backwards (new video_t, counter reset) re-baselines
//     at the current values instead of producing a huge unsigned difference.
// Unlike the native dock, a window with no counted frames reports the
// percentage as N/A instead of "0.0%" (never a fabricated value).
#pragma once

#include "metric.hpp"

#include <cstdint>

namespace msd {

struct FrameCounts {
	Metric<uint64_t> affected;
	Metric<uint64_t> total;
	Metric<double> percent; // affected * 100 / total; N/A while total == 0
};

class FrameWindow {
public:
	void baseline(uint32_t total, uint32_t affected)
	{
		firstTotal_ = total;
		firstAffected_ = affected;
		have_ = true;
	}
	void invalidate() { have_ = false; }
	bool hasBaseline() const { return have_; }
	uint32_t firstTotal() const { return firstTotal_; }
	uint32_t firstAffected() const { return firstAffected_; }

	// Counts since the baseline. Returns true in `rebased` when the sample
	// itself had to become the baseline (no baseline yet, or a counter
	// decreased).
	FrameCounts sample(uint32_t total, uint32_t affected, bool *rebased = nullptr);

private:
	bool have_ = false;
	uint32_t firstTotal_ = 0;
	uint32_t firstAffected_ = 0;
};

} // namespace msd
