// SPDX-License-Identifier: GPL-2.0-or-later
#include "aitum-bridge.hpp"
#include "../plugin-log.hpp"

#include <core/naming.hpp>

#include <obs.hpp>
// Only this translation unit includes the obs-websocket API header (it
// defines a static per-TU proc handler cache).
#include <obs-websocket-api.h>

namespace msd {

namespace {
constexpr unsigned int kWebsocketSuccess = 100; // obs-websocket RequestStatus::Success
}

bool AitumBridge::aitumLoaded() const
{
	return obs_get_module("aitum-multistream") != nullptr;
}

std::optional<std::vector<ConfiguredDestination>> AitumBridge::queryDestinations()
{
	if (!aitumLoaded())
		return std::nullopt;
	if (obs_websocket_get_api_version() == 0) {
		if (!loggedUnavailable_) {
			msd::log(LOG_INFO, "obs-websocket API not available; Aitum destinations are shown only while running");
			loggedUnavailable_ = true;
		}
		return std::nullopt;
	}

	OBSDataAutoRelease request = obs_data_create();
	OBSDataAutoRelease requestData = obs_data_create();
	obs_data_set_string(request, "vendorName", kAitumVendorName);
	obs_data_set_string(request, "requestType", "get_outputs");
	obs_data_set_obj(request, "requestData", requestData);

	obs_websocket_request_response *response = obs_websocket_call_request("CallVendorRequest", request);
	if (!response)
		return std::nullopt;

	std::optional<std::vector<ConfiguredDestination>> result;
	if (response->status_code == kWebsocketSuccess && response->response_data) {
		OBSDataAutoRelease data = obs_data_create_from_json(response->response_data);
		OBSDataAutoRelease responseData = data ? obs_data_get_obj(data, "responseData") : nullptr;
		if (responseData && obs_data_get_bool(responseData, "success")) {
			std::vector<ConfiguredDestination> list;
			OBSDataArrayAutoRelease outputs = obs_data_get_array(responseData, "outputs");
			const size_t count = outputs ? obs_data_array_count(outputs) : 0;
			for (size_t i = 0; i < count; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(outputs, i);
				const char *name = obs_data_get_string(item, "name");
				if (!name || !*name)
					continue;
				ConfiguredDestination d;
				d.kind = ProviderKind::Aitum;
				d.name = name;
				d.vertical = obs_data_get_bool(item, "vertical");
				list.push_back(std::move(d));
			}
			result = std::move(list);
			loggedUnavailable_ = false;
		}
	}
	if (!result && !loggedUnavailable_) {
		msd::log(LOG_INFO, "Aitum vendor request get_outputs unavailable (status %u)", response->status_code);
		loggedUnavailable_ = true;
	}
	obs_websocket_request_response_free(response);
	return result;
}

} // namespace msd
