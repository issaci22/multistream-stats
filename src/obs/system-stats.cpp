// SPDX-License-Identifier: GPL-2.0-or-later
#include "system-stats.hpp"
#include "../plugin-log.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

namespace msd {

namespace {
constexpr uint64_t kDiskQueryIntervalNs = 10'000'000'000ULL;

const char *windowName(LagWindow w)
{
	switch (w) {
	case LagWindow::SinceLoaded:
		return "loaded";
	case LagWindow::SinceVideoReset:
		return "video_reset";
	case LagWindow::SinceUserReset:
		return "user_reset";
	}
	return "?";
}
} // namespace

SystemStats::SystemStats()
{
	cpu_ = os_cpu_usage_info_start();
	applyBaseline(readCounters(), LagWindow::SinceLoaded);
	if (signal_handler_t *sh = obs_get_signal_handler()) {
		signal_handler_connect(sh, "video_reset", &SystemStats::onVideoReset, this);
		signalConnected_ = true;
	}
}

SystemStats::~SystemStats()
{
	// Disconnect first: signal_handler_disconnect waits for a callback that is
	// running on another thread, so `this` is never used after destruction.
	if (signalConnected_) {
		if (signal_handler_t *sh = obs_get_signal_handler())
			signal_handler_disconnect(sh, "video_reset", &SystemStats::onVideoReset, this);
	}
	if (cpu_)
		os_cpu_usage_info_destroy(cpu_);
}

SystemStats::Counters SystemStats::readCounters()
{
	Counters c;
	c.rendered = obs_get_total_frames();
	c.lagged = obs_get_lagged_frames();
	if (video_t *video = obs_get_video()) {
		c.haveVideo = true;
		c.encoded = video_output_get_total_frames(video);
		c.skipped = video_output_get_skipped_frames(video);
	}
	return c;
}

void SystemStats::onVideoReset(void *data, calldata_t *)
{
	// Emitted by obs_reset_video() (OBSBasic::ResetVideo -> native
	// InitializeValues right after). Capture the counters at this exact point;
	// the UI-thread sample() applies them.
	auto *self = static_cast<SystemStats *>(data);
	const Counters c = readCounters();
	std::lock_guard<std::mutex> lock(self->pendingMutex_);
	self->pendingReset_ = c;
}

void SystemStats::applyBaseline(const Counters &c, LagWindow window)
{
	render_.baseline(c.rendered, c.lagged);
	if (c.haveVideo)
		encode_.baseline(c.encoded, c.skipped);
	else
		encode_.invalidate();
	window_ = window;
	msd::log(LOG_INFO, "lag window: baseline (%s) rendered=%u lagged=%u encoded=%u skipped=%u", windowName(window),
		 c.rendered, c.lagged, c.encoded, c.skipped);
}

void SystemStats::resetBaselines()
{
	{
		std::lock_guard<std::mutex> lock(pendingMutex_);
		pendingReset_.reset();
	}
	applyBaseline(readCounters(), LagWindow::SinceUserReset);
}

SystemSnapshot SystemStats::sample(uint64_t nowNs)
{
	std::optional<Counters> pending;
	{
		std::lock_guard<std::mutex> lock(pendingMutex_);
		pending.swap(pendingReset_);
	}
	if (pending)
		applyBaseline(*pending, LagWindow::SinceVideoReset);

	SystemSnapshot s;
	s.lagWindow = window_;

	if (cpu_)
		s.cpuPercent = Metric<double>::of(os_cpu_usage_info_query(cpu_));
	else
		s.cpuPercent = Metric<double>::na(Unavailable::NotSupported);

	const uint64_t rss = os_get_proc_resident_size();
	s.memoryBytes = rss > 0 ? Metric<uint64_t>::of(rss) : Metric<uint64_t>::na(Unavailable::NotSupported);

	if (lastDiskQueryNs_ == 0 || nowNs - lastDiskQueryNs_ >= kDiskQueryIntervalNs) {
		lastDiskQueryNs_ = nowNs;
		char *path = obs_frontend_get_current_record_output_path();
		if (path && *path)
			disk_ = Metric<uint64_t>::of(os_get_free_disk_space(path));
		else
			disk_ = Metric<uint64_t>::na(Unavailable::Unknown);
		bfree(path);
	}
	s.diskFreeBytes = disk_;

	struct obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.fps_den > 0)
		s.targetFps = (double)ovi.fps_num / (double)ovi.fps_den;

	s.fps = Metric<double>::of(obs_get_active_fps());
	s.fpsHealth = fpsHealth(s.fps, s.targetFps);

	s.renderMs = Metric<double>::of((double)obs_get_average_frame_time_ns() / 1'000'000.0);
	s.renderHealth = renderTimeHealth(s.renderMs, s.targetFps);

	const Counters now = readCounters();

	// Rendering lag: frames the render thread could not produce in time.
	bool rebased = false;
	const FrameCounts render = render_.sample(now.rendered, now.lagged, &rebased);
	if (rebased)
		msd::log(LOG_INFO, "lag window: rendering counters went backwards, re-baselined at rendered=%u lagged=%u",
			 now.rendered, now.lagged);
	s.renderTotal = render.total;
	s.renderLagged = render.affected;
	s.renderLagPct = render.percent;
	s.renderLagHealth = percentHealth(s.renderLagPct);

	// Encoding lag on the main canvas pipeline. A new video_t (after a video
	// reset) restarts its counters; FrameWindow re-baselines instead of
	// underflowing.
	if (now.haveVideo) {
		const FrameCounts enc = encode_.sample(now.encoded, now.skipped);
		s.encodeTotal = enc.total;
		s.encodeSkipped = enc.affected;
		s.encodeLagPct = enc.percent;
	} else {
		s.encodeTotal = Metric<uint64_t>::na(Unavailable::NotActive);
		s.encodeSkipped = s.encodeTotal;
		s.encodeLagPct = Metric<double>::na(Unavailable::NotActive);
	}
	s.encodeLagHealth = percentHealth(s.encodeLagPct);
	return s;
}

} // namespace msd
