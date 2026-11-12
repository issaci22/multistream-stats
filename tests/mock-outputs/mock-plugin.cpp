// SPDX-License-Identifier: GPL-2.0-or-later
//
// msd-mock-outputs: TEST-ONLY module. Never installed into a real OBS.
#include "harness.hpp"
#include "mock-output.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <QFileInfo>
#include <QString>

#include <atomic>
#include <cstdlib>

OBS_DECLARE_MODULE()

namespace {

msdmock::Harness *g_harness = nullptr;

// Optional startup render lag (MSD_MOCK_STARTUP_LAG_MS / _TICKS): the graphics
// thread sleeps in a tick callback while OBS is still loading, which produces
// real "frames missed due to rendering lag" BEFORE the scene collection is
// loaded. OBS's native Stats dock excludes these (its window starts right
// before FINISHED_LOADING); 1.0.1 must exclude them too (1.0.0 counted them).
std::atomic<bool> g_lagActive{false};
std::atomic<int> g_lagTicksLeft{0};
int g_lagMs = 0;
bool g_lagRegistered = false;

void lagTick(void *, float)
{
	if (!g_lagActive.load())
		return;
	if (g_lagTicksLeft.fetch_sub(1) <= 0) {
		g_lagActive.store(false);
		return;
	}
	os_sleep_ms((uint32_t)g_lagMs);
}

void stopLag(const char *why)
{
	if (g_lagActive.exchange(false))
		blog(LOG_INFO, "[msd-mock] startup lag stopped (%s): lagged=%u rendered=%u", why, obs_get_lagged_frames(),
		     obs_get_total_frames());
}

void onEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
		stopLag("scene collection loaded");
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		stopLag("finished loading");
		blog(LOG_INFO, "[msd-mock] at FINISHED_LOADING: lagged=%u rendered=%u", obs_get_lagged_frames(),
		     obs_get_total_frames());
	}
	if (!g_harness)
		return;
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		g_harness->begin();
	else if (event == OBS_FRONTEND_EVENT_EXIT)
		g_harness->releaseAll(); // release outputs before modules unload
}

int envInt(const char *name, int fallback)
{
	const char *v = std::getenv(name);
	return v && *v ? std::atoi(v) : fallback;
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
	return "MSD mock outputs (test only)";
}

bool obs_module_load(void)
{
	msdmock::registerMockOutputs();

	// Safety: the harness only runs inside the isolated workspace test copy.
	const char *self = obs_get_module_binary_path(obs_current_module());
	// OBS reports the path relative to bin\64bit; resolve it first.
	const QString selfPath = QFileInfo(QString::fromUtf8(self ? self : "")).absoluteFilePath().replace('\\', '/');
	const char *scenario = std::getenv("MSD_MOCK_SCENARIO");
	if (!selfPath.contains(QStringLiteral("/test-env/"))) {
		blog(LOG_WARNING, "[msd-mock] not inside a workspace test-env (%s): harness disabled", self ? self : "?");
		return true;
	}
	if (scenario && *scenario) {
		g_harness = new msdmock::Harness();
		const char *out = std::getenv("MSD_TEST_OUT");
		g_harness->load(QString::fromUtf8(scenario), QString::fromUtf8(out ? out : ""));
	}
	g_lagMs = envInt("MSD_MOCK_STARTUP_LAG_MS", 0);
	if (g_lagMs > 0) {
		g_lagTicksLeft.store(envInt("MSD_MOCK_STARTUP_LAG_TICKS", 10));
		g_lagActive.store(true);
		obs_add_tick_callback(lagTick, nullptr);
		g_lagRegistered = true;
		blog(LOG_INFO, "[msd-mock] startup lag enabled: %d ticks x %d ms (lagged so far %u)", g_lagTicksLeft.load(),
		     g_lagMs, obs_get_lagged_frames());
	}
	if (g_harness || g_lagRegistered)
		obs_frontend_add_event_callback(onEvent, nullptr);
	blog(LOG_INFO, "[msd-mock] loaded (harness %s)", g_harness ? "enabled" : "disabled");
	return true;
}

void obs_module_unload(void)
{
	if (g_lagRegistered) {
		g_lagActive.store(false);
		obs_remove_tick_callback(lagTick, nullptr);
		g_lagRegistered = false;
	}
	if (g_harness) {
		g_harness->releaseAll();
		delete g_harness;
		g_harness = nullptr;
	}
}
