// SPDX-License-Identifier: GPL-2.0-or-later
#include "naming.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace msd {

bool startsWith(const std::string &s, const char *prefix)
{
	const size_t n = std::strlen(prefix);
	return s.size() >= n && s.compare(0, n, prefix) == 0;
}

std::string aitumDestinationFromOutputName(const std::string &outputName)
{
	if (!startsWith(outputName, kAitumOutputPrefix))
		return {};
	return outputName.substr(std::strlen(kAitumOutputPrefix));
}

ProviderKind classifyByOutputName(const std::string &outputName)
{
	if (startsWith(outputName, kAitumOutputPrefix) && outputName.size() > std::strlen(kAitumOutputPrefix))
		return ProviderKind::Aitum;
	if (outputName == kMultiRtmpOutputName)
		return ProviderKind::MultiRtmp;
	return ProviderKind::Generic;
}

OutputCapabilities capabilitiesForOutputType(const std::string &typeId)
{
	OutputCapabilities caps;
	if (typeId == "rtmp_output") {
		// rtmp-stream.c: get_dropped_frames, get_congestion, get_connect_time_ms
		caps.droppedFrames = true;
		caps.congestion = true;
		caps.connectTime = true;
	} else if (typeId == "whip_output") {
		// whip-output.cpp: get_connect_time_ms only
		caps.connectTime = true;
	}
	// ffmpeg_mpegts_muxer (SRT/RIST): total bytes only. Unknown types: assume
	// nothing beyond total bytes; non-zero values still prove support.
	return caps;
}

std::string hostFromUrl(const std::string &url)
{
	std::string rest = url;
	const size_t scheme = rest.find("://");
	if (scheme != std::string::npos)
		rest = rest.substr(scheme + 3);

	// Authority ends at the first '/', '?' or '#'.
	const size_t end = rest.find_first_of("/?#");
	std::string authority = end == std::string::npos ? rest : rest.substr(0, end);

	// Drop credentials.
	const size_t at = authority.rfind('@');
	if (at != std::string::npos)
		authority = authority.substr(at + 1);

	std::string host;
	if (!authority.empty() && authority[0] == '[') { // IPv6 literal
		const size_t close = authority.find(']');
		host = close == std::string::npos ? authority : authority.substr(0, close + 1);
	} else {
		const size_t colon = authority.find(':');
		host = colon == std::string::npos ? authority : authority.substr(0, colon);
	}

	std::transform(host.begin(), host.end(), host.begin(),
		       [](unsigned char c) { return (char)std::tolower(c); });

	// Reject anything that doesn't look like a hostname.
	for (unsigned char c : host) {
		if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':' || c == '[' || c == ']'))
			return {};
	}
	return host;
}

std::string aitumRowKey(const std::string &destination)
{
	return "aitum:" + destination;
}

std::string baseRowKey(const OutputObservation &obs)
{
	switch (obs.kind) {
	case ProviderKind::Primary:
		return primaryRowKey();
	case ProviderKind::Aitum:
		return aitumRowKey(obs.destination);
	case ProviderKind::MultiRtmp:
		return "multirtmp:" + obs.ingestHost;
	case ProviderKind::Generic:
	default:
		return "output:" + obs.outputName + "|" + obs.ingestHost;
	}
}

} // namespace msd
