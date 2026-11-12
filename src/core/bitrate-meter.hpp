// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "metric.hpp"

#include <cstdint>
#include <deque>

namespace msd {

// Turns a raw counter that the output plugin may reset (rtmp_output resets
// total_bytes_sent and dropped_frames on every (re)connect) into a monotonic
// per-session counter. A decrease is treated as a reset: the previous value is
// folded into an offset.
class CounterAccumulator {
public:
	void reset();
	uint64_t update(uint64_t raw);
	uint64_t total() const { return offset_ + last_; }
	bool hasValue() const { return has_; }

private:
	uint64_t offset_ = 0;
	uint64_t last_ = 0;
	bool has_ = false;
};

// Current bitrate from successive byte counts, using the same formula as OBS's
// native Stats dock (kbps = Δbytes·8 / Δt / 1000) over a short sliding window
// (default 2 s, the native dock's refresh interval) to smooth keyframe bursts.
class BitrateMeter {
public:
	BitrateMeter() = default;
	explicit BitrateMeter(uint64_t windowNs) : windowNs_(windowNs) {}

	void reset();
	// bytes must be monotonic within a session (use CounterAccumulator).
	void addSample(uint64_t timestampNs, uint64_t bytes);
	// N/A (NotMeasured) until two samples at least 10 ms apart exist.
	Metric<double> currentKbps() const;

private:
	struct Point {
		uint64_t ns;
		uint64_t bytes;
	};
	std::deque<Point> points_;
	uint64_t windowNs_ = 2'000'000'000ULL;
};

// Average bitrate over a session: bytes·8 / elapsed.
Metric<double> averageKbps(uint64_t bytes, uint64_t elapsedNs);

// Mirrors OBSBasicStatusBar's congestion smoothing: each sample is averaged
// with the previous one (never below the current value), and the mean of the
// last 4 smoothed samples is reported.
class CongestionSmoother {
public:
	void reset();
	void addSample(float congestion);
	Metric<double> value() const;

private:
	std::deque<float> window_;
	float last_ = 0.0f;
	bool hasLast_ = false;
};

// Tracks dropped/total frame deltas over a sliding window so the dock can tell
// whether frames are being dropped *right now*, not just cumulatively.
class RecentDropWindow {
public:
	RecentDropWindow() = default;
	explicit RecentDropWindow(uint64_t windowNs) : windowNs_(windowNs) {}
	void reset();
	void addSample(uint64_t timestampNs, uint64_t dropped, uint64_t total);
	// Percentage of frames dropped within the window; N/A if no frames in window.
	Metric<double> recentPercent() const;
	uint64_t recentDropped() const;

private:
	struct Point {
		uint64_t ns;
		uint64_t dropped;
		uint64_t total;
	};
	std::deque<Point> points_;
	uint64_t windowNs_ = 10'000'000'000ULL;
};

} // namespace msd
