// SPDX-License-Identifier: GPL-2.0-or-later
//
// Simulated streaming output types for isolated testing. They are real libobs
// encoded service outputs (encoders run, packets are delivered), but packets
// are discarded instead of being sent anywhere - no network is used.
//
//   msd_mock_stream        implements total bytes, dropped frames, congestion
//                          and connect time (like rtmp_output)
//   msd_mock_stream_basic  implements total bytes only (like the SRT output)
#pragma once

#include <obs.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace msdmock {

struct MockStream {
	obs_output_t *output = nullptr;
	std::atomic<uint64_t> bytes{0};
	std::atomic<int> dropped{0};
	std::atomic<float> congestion{0.0f};
	std::atomic<int> connectMs{-1};
	std::atomic<double> dropRate{0.0}; // fraction of non-keyframe video packets dropped
	std::atomic<bool> unreachable{false}; // connection attempts fail
	std::atomic<bool> capturing{false};
	std::atomic<bool> everConnected{false};
	std::thread connectThread;
	std::mutex threadMutex;
	uint64_t rng = 0x9E3779B97F4A7C15ULL;
};

void registerMockOutputs();

// Simulate a network failure on a live mock output (libobs then reconnects).
void simulateDisconnect(obs_output_t *output);
// Join any in-flight simulated connect before the output is released.
void prepareRelease(obs_output_t *output);
MockStream *mockOf(obs_output_t *output);

} // namespace msdmock
