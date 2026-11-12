// SPDX-License-Identifier: GPL-2.0-or-later
//
// Optional integration with Aitum Multistream's *public* interface: its
// obs-websocket vendor "aitum-multistream" and request "get_outputs", called
// in-process through obs-websocket's documented C API (obs-websocket-api.h).
// It only provides the list of configured destinations (name + vertical flag),
// so destinations can be shown as "Stopped" before their first start and
// removed after being renamed/deleted. All live statistics come from the
// standard libobs output objects Aitum creates.
//
// UI thread only (obs-websocket-api.h caches its proc handler unsynchronised).
#pragma once

#include <core/observation.hpp>

#include <optional>
#include <vector>

namespace msd {

class AitumBridge {
public:
	// True if the aitum-multistream module is loaded in this OBS instance.
	bool aitumLoaded() const;

	// nullopt if obs-websocket or the Aitum vendor is unavailable.
	std::optional<std::vector<ConfiguredDestination>> queryDestinations();

private:
	bool loggedUnavailable_ = false;
};

} // namespace msd
