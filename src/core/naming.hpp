// SPDX-License-Identifier: GPL-2.0-or-later
//
// Provider recognition and naming rules. Every rule here is backed by
// observed plugin behaviour documented in FINDINGS.md.
#pragma once

#include "observation.hpp"

#include <string>

namespace msd {

// Aitum Multistream names its outputs "aitum_multi_output_<Destination>".
inline constexpr const char *kAitumOutputPrefix = "aitum_multi_output_";
// sorayuki/obs-multi-rtmp names every output "multi-output".
inline constexpr const char *kMultiRtmpOutputName = "multi-output";
// Aitum's obs-websocket vendor name.
inline constexpr const char *kAitumVendorName = "aitum-multistream";

bool startsWith(const std::string &s, const char *prefix);

// "aitum_multi_output_YouTube Horizontal" -> "YouTube Horizontal"; "" if not Aitum.
std::string aitumDestinationFromOutputName(const std::string &outputName);

// Classify by output name (the primary output is identified by pointer
// equality in the adapter layer, not by name).
ProviderKind classifyByOutputName(const std::string &outputName);

// Known statistic support per output type id (OBS 33.0.1 source).
OutputCapabilities capabilitiesForOutputType(const std::string &typeId);

// Extracts only the host from a server URL, dropping scheme, credentials,
// port, path and query. "rtmps://user:pw@a.rtmps.youtube.com:443/live2?x" ->
// "a.rtmps.youtube.com". Returns "" if no host can be found.
std::string hostFromUrl(const std::string &url);

// Base (un-disambiguated) registry key for an observation.
std::string baseRowKey(const OutputObservation &obs);
std::string aitumRowKey(const std::string &destination);
inline const char *primaryRowKey()
{
	return "primary";
}

} // namespace msd
