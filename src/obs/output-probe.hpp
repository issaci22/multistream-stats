// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reads public libobs state of one output. All functions must be called on
// the UI thread with a strong reference to the output held by the caller.
#pragma once

#include <core/observation.hpp>

#include <obs.h>

#include <string>

namespace msd {

// Streaming outputs are those that use a service (RTMP/SRT/RIST/WHIP...).
// Recording, replay buffer and virtual camera outputs are excluded.
bool isStreamingOutput(obs_output_t *output);

struct OutputDescription {
	ProviderKind kind = ProviderKind::Generic;
	std::string outputName;
	std::string typeId;
	std::string destination;
	std::string serviceName;
	std::string ingestHost;
	OutputCapabilities caps;
};

OutputDescription describeOutput(obs_output_t *output, bool isPrimary);
OutputSample sampleOutput(obs_output_t *output, uint64_t nowNs);

} // namespace msd
