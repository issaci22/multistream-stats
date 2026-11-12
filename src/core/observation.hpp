// SPDX-License-Identifier: GPL-2.0-or-later
//
// Raw, per-tick observations of one libobs output, produced by the OBS adapter
// layer (src/obs) and consumed by the platform-independent SessionRegistry.
// Keeping this boundary free of libobs types makes the statistics logic
// testable with synthetic data (tests/unit).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace msd {

// Mirrors libobs' OBS_OUTPUT_* stop codes (obs-defs.h). The adapter layer
// static_asserts that these values match the real headers.
namespace StopCode {
constexpr int Success = 0;
constexpr int BadPath = -1;
constexpr int ConnectFailed = -2;
constexpr int InvalidStream = -3;
constexpr int Error = -4;
constexpr int Disconnected = -5;
constexpr int Unsupported = -6;
constexpr int NoSpace = -7;
constexpr int EncodeError = -8;
constexpr int HdrDisabled = -9;
} // namespace StopCode

// Which integration recognised the output.
enum class ProviderKind {
	Primary,  // OBS's own stream output (obs_frontend_get_streaming_output)
	Aitum,    // Aitum Multistream ("aitum_multi_output_<Name>")
	MultiRtmp, // sorayuki obs-multi-rtmp ("multi-output")
	Generic,  // any other service (streaming) output
};

// Which statistics the output type is known to implement (see FINDINGS.md).
// For unimplemented callbacks libobs returns 0 / -1, which is
// indistinguishable from a real 0, so these flags decide N/A vs value.
struct OutputCapabilities {
	bool droppedFrames = false;
	bool congestion = false;
	bool connectTime = false;
};

enum class OutputEventType {
	Starting,         // "starting": obs_output_start succeeded, connecting
	Activate,         // "activate": data capture began
	Start,            // "start": connected and streaming
	Stopping,         // "stopping": stop requested
	Stop,             // "stop": session ended (code)
	Reconnect,        // "reconnect": connection lost, retrying
	ReconnectSuccess, // "reconnect_success"
	Deactivate,       // "deactivate"
};

struct OutputEvent {
	OutputEventType type = OutputEventType::Start;
	uint64_t timestampNs = 0;
	int stopCode = 0;          // Stop only
	std::string lastError;     // Stop only (obs_output_get_last_error at stop time)
	bool hasFinalCounters = false; // Stop only: counters captured inside the callback
	uint64_t finalBytes = 0;
	int finalDropped = 0;
	int finalTotalFrames = 0;
};

// Static description of the encoder pipeline feeding an output.
struct EncoderInfo {
	bool present = false;
	std::string displayName; // e.g. "NVIDIA NVENC HEVC"
	std::string codec;       // e.g. "hevc"
	int targetKbps = -1;     // configured bitrate, -1 if not set / not CBR/VBR/ABR
	std::string audioDisplayName;
	int audioKbps = -1;
	bool dedicatedPipeline = false; // encoder video_t != main canvas video_t
	std::string canvasName;         // "" = unknown
	bool canvasIsMain = false;
	double fps = 0.0;               // 0 = unknown
	uint64_t pipelineSkipped = 0;   // valid if dedicatedPipeline
	uint64_t pipelineTotal = 0;
};

// One poll of an output. Only produced while the output object exists.
struct OutputSample {
	uint64_t timestampNs = 0;
	bool active = false;       // obs_output_active (includes reconnecting)
	bool reconnecting = false; // obs_output_reconnecting
	uint64_t totalBytes = 0;   // obs_output_get_total_bytes
	int framesDropped = 0;     // obs_output_get_frames_dropped (only meaningful if caps.droppedFrames)
	int totalFrames = 0;       // obs_output_get_total_frames
	float congestion = 0.0f;   // obs_output_get_congestion (only meaningful if caps.congestion)
	int connectTimeMs = -1;    // obs_output_get_connect_time_ms
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t delaySec = 0;     // obs_output_get_active_delay
	EncoderInfo encoder;
};

// Everything the adapter layer knows about one live output object this tick.
struct OutputObservation {
	uint64_t instanceId = 0;   // unique per output *object* (never reused)
	ProviderKind kind = ProviderKind::Generic;
	std::string outputName;    // libobs output name
	std::string typeId;        // e.g. "rtmp_output"
	std::string destination;   // provider-specific destination name (Aitum suffix, etc.)
	std::string serviceName;   // e.g. "Twitch" for rtmp_common, may be empty
	std::string ingestHost;    // host part of the server URL only (never the key)
	OutputCapabilities caps;
	bool firstSeenActive = false; // was already active when first discovered
	bool initialScan = false;     // discovered during the very first scan (start time unknown)
	std::vector<OutputEvent> events; // signals received since last tick, in order
	OutputSample sample;
};

// An instance that disappeared (object destroyed) since the last tick, with
// any events that were still queued.
struct VanishedOutput {
	uint64_t instanceId = 0;
	std::vector<OutputEvent> events;
};

// A destination configured in a multistream plugin, live or not.
struct ConfiguredDestination {
	ProviderKind kind = ProviderKind::Aitum;
	std::string name;
	bool vertical = false;
};

} // namespace msd
