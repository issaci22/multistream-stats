// SPDX-License-Identifier: GPL-2.0-or-later
//
// Multi-Stream Statistics dock for OBS Studio.
//
// Lifecycle:
//   obs_module_load      create collector + dock, register dock and frontend callback
//   FINISHED_LOADING     start monitoring (frontend output handler exists now)
//   EXIT                 stop monitoring: timers stopped, every output signal
//                        disconnected, collector destroyed, callback removed
//   obs_module_unload    same teardown if EXIT was never delivered (idempotent)
//
// The dock widget itself is owned by OBS (obs_frontend_add_dock_by_id) and is
// destroyed with the main window; it only holds a QPointer to the collector.

#include "plugin-log.hpp"
#include "obs/stats-collector.hpp"
#include "ui/stats-dock.hpp"
#include "ui/theme.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMainWindow>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-multistream-stats", "en-US")

namespace {

constexpr const char *kDockId = "multistream-stats-dock";

msd::StatsCollector *g_collector = nullptr;
QPointer<msdui::StatsDock> g_dock;
bool g_callbackRegistered = false;

void teardown()
{
	if (g_dock)
		g_dock->saveState();
	if (g_collector) {
		g_collector->shutdown();
		delete g_collector; // dock's QPointer becomes null, Qt connections drop
		g_collector = nullptr;
	}
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (g_collector)
			g_collector->start();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		teardown();
		if (g_callbackRegistered) {
			obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
			g_callbackRegistered = false;
		}
		break;
	default:
		if (g_collector)
			g_collector->handleFrontendEvent(event);
		break;
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("PluginName");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("PluginDescription");
}

bool obs_module_load(void)
{
	msd::log(LOG_INFO, "loaded version %s (libobs API %d.%d.%d, running OBS %s)", MSD_PLUGIN_VERSION,
		 LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER, obs_get_version_string());

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		msd::log(LOG_WARNING, "no main window; dock not created");
		return true;
	}

	g_collector = new msd::StatsCollector();
	auto *dock = new msdui::StatsDock(g_collector, mainWindow);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("DockTitle"), dock)) {
		msd::log(LOG_WARNING, "could not register dock '%s'", kDockId);
		delete dock;
		delete g_collector;
		g_collector = nullptr;
		return true;
	}
	g_dock = dock;

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	g_callbackRegistered = true;
	return true;
}

void obs_module_unload(void)
{
	// Normally already done on OBS_FRONTEND_EVENT_EXIT. This only touches
	// libobs (signal disconnects are safe even after outputs are destroyed).
	teardown();
	msd::log(LOG_INFO, "unloaded");
}
