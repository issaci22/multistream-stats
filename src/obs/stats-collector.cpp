// SPDX-License-Identifier: GPL-2.0-or-later
#include "stats-collector.hpp"
#include "output-probe.hpp"
#include "../plugin-log.hpp"

#include <core/aggregate.hpp>
#include <core/format.hpp>

#include <obs.hpp>
#include <util/platform.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>

namespace msd {

namespace {

constexpr int kDiscoveryIntervalMs = 250;
constexpr int kStatsIntervalMs = 1000;
constexpr uint64_t kAitumRefreshNs = 5'000'000'000ULL;

struct EnumContext {
	const std::unordered_set<const void *> *known = nullptr;
	std::vector<obs_weak_output_t *> fresh;
};

// Runs while libobs holds outputs_mutex: identity check + weak ref only.
bool enumOutputsProc(void *param, obs_output_t *output)
{
	auto *ctx = static_cast<EnumContext *>(param);
	if (ctx->known->find(output) == ctx->known->end())
		ctx->fresh.push_back(obs_output_get_weak_output(output));
	return true;
}

const char *stateName(OutputState s)
{
	switch (s) {
	case OutputState::Idle:
		return "Idle";
	case OutputState::Connecting:
		return "Connecting";
	case OutputState::Live:
		return "Live";
	case OutputState::Reconnecting:
		return "Reconnecting";
	case OutputState::Stopping:
		return "Stopping";
	case OutputState::Stopped:
		return "Stopped";
	case OutputState::Disconnected:
		return "Disconnected";
	case OutputState::Error:
		return "Error";
	}
	return "?";
}

template<typename T> std::string metricText(const Metric<T> &m)
{
	if (!m.has())
		return "na";
	if constexpr (std::is_floating_point_v<T>) {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.1f", (double)m.value());
		return buf;
	} else {
		return std::to_string(m.value());
	}
}

} // namespace

StatsCollector::StatsCollector(QObject *parent) : QObject(parent)
{
	discoveryTimer_.setInterval(kDiscoveryIntervalMs);
	discoveryTimer_.setTimerType(Qt::CoarseTimer);
	statsTimer_.setInterval(kStatsIntervalMs);
	statsTimer_.setTimerType(Qt::CoarseTimer);
	connect(&discoveryTimer_, &QTimer::timeout, this, &StatsCollector::onDiscoveryTimer);
	connect(&statsTimer_, &QTimer::timeout, this, &StatsCollector::onStatsTimer);

	const char *dbg = std::getenv("MSD_DEBUG_TICKS");
	debugTicks_ = dbg && *dbg && *dbg != '0';
}

StatsCollector::~StatsCollector()
{
	shutdown();
}

void StatsCollector::start()
{
	if (started_ || shutdown_)
		return;
	started_ = true;
	system_ = std::make_unique<SystemStats>();

	const uint64_t now = os_gettime_ns();
	refreshAitumConfig(now, true);
	discover(); // initial scan: outputs already live get "start time unknown"
	firstScanDone_ = true;

	discoveryTimer_.start();
	statsTimer_.start();
	onStatsTimer();

	msd::log(LOG_INFO, "monitoring started (discovery %d ms, statistics %d ms, Aitum %s)", kDiscoveryIntervalMs,
		 kStatsIntervalMs, aitum_.aitumLoaded() ? "detected" : "not loaded");
}

void StatsCollector::shutdown()
{
	if (shutdown_)
		return;
	shutdown_ = true;
	discoveryTimer_.stop();
	statsTimer_.stop();

	// Destroying trackers disconnects every output signal. This must happen
	// before OBS unloads modules, because leftover outputs are destroyed
	// after module unload (obs_shutdown) and could otherwise call into this
	// DLL after it is gone.
	trackers_.clear();
	for (const Ignored &i : ignored_)
		obs_weak_output_release(i.weak);
	ignored_.clear();
	vanished_.clear();
	system_.reset();

	if (tickCount_ > 0)
		msd::log(LOG_INFO, "statistics ticks: %llu, avg %.1f us, max %.1f us (dock updates: %llu, avg %.1f us, max %.1f us)",
			 (unsigned long long)tickCount_, (double)tickNsTotal_ / (double)tickCount_ / 1000.0,
			 (double)tickNsMax_ / 1000.0, (unsigned long long)uiCount_,
			 uiCount_ ? (double)uiNsTotal_ / (double)uiCount_ / 1000.0 : 0.0, (double)uiNsMax_ / 1000.0);
	if (started_)
		msd::log(LOG_INFO, "monitoring stopped");
}

void StatsCollector::setDisplayActive(bool active)
{
	if (displayActive_ == active)
		return;
	displayActive_ = active;
	if (active && started_ && !shutdown_) {
		const uint64_t now = os_gettime_ns();
		refreshAitumConfig(now, true);
		publish(now);
	}
}

void StatsCollector::handleFrontendEvent(enum obs_frontend_event event)
{
	if (!started_ || shutdown_)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		// The primary output exists but is not connected yet: track it now
		// so its "starting"/"start" signals are observed exactly.
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		discover();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
	case OBS_FRONTEND_EVENT_PROFILE_RENAMED:
		discover();
		refreshAitumConfig(os_gettime_ns(), true);
		break;
	default:
		break;
	}
}

void StatsCollector::resetStatistics()
{
	if (shutdown_)
		return;
	registry_.resetStatistics();
	if (system_)
		system_->resetBaselines();
	msd::log(LOG_INFO, "statistics reset by user");
	publish(os_gettime_ns());
}

void StatsCollector::clearEnded()
{
	if (shutdown_)
		return;
	registry_.clearEnded();
	publish(os_gettime_ns());
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------
void StatsCollector::pruneDeadTrackers()
{
	for (auto it = trackers_.begin(); it != trackers_.end();) {
		if (obs_output_t *output = (*it)->acquire()) {
			obs_output_release(output);
			++it;
			continue;
		}
		// Destroyed: disconnect first (waits for in-flight callbacks), then
		// collect every event it delivered.
		(*it)->detach();
		VanishedOutput v;
		v.instanceId = (*it)->instanceId();
		v.events = (*it)->drainEvents();
		vanished_.push_back(std::move(v));
		it = trackers_.erase(it);
	}
}

void StatsCollector::pruneIgnored()
{
	for (auto it = ignored_.begin(); it != ignored_.end();) {
		if (obs_output_t *output = obs_weak_output_get_output(it->weak)) {
			obs_output_release(output);
			++it;
			continue;
		}
		obs_weak_output_release(it->weak);
		it = ignored_.erase(it);
	}
}

void StatsCollector::discover()
{
	if (shutdown_)
		return;

	// Prune first so a destroyed object's address can never mask a new one.
	pruneDeadTrackers();
	pruneIgnored();

	std::unordered_set<const void *> known;
	known.reserve(trackers_.size() + ignored_.size());
	for (const auto &t : trackers_)
		known.insert(t->identity());
	for (const Ignored &i : ignored_)
		known.insert(i.identity);

	EnumContext ctx;
	ctx.known = &known;
	obs_enum_outputs(enumOutputsProc, &ctx);

	const bool initial = !firstScanDone_;
	for (obs_weak_output_t *weak : ctx.fresh) {
		obs_output_t *output = obs_weak_output_get_output(weak);
		if (!output) {
			obs_weak_output_release(weak);
			continue;
		}
		if (isStreamingOutput(output)) {
			msd::log(LOG_INFO, "tracking streaming output '%s' (%s)", obs_output_get_name(output),
				 obs_output_get_id(output));
			trackers_.push_back(std::make_unique<OutputTracker>(nextInstanceId_++, output, initial));
			obs_weak_output_release(weak);
		} else {
			ignored_.push_back({weak, output});
		}
		obs_output_release(output);
	}
}

void StatsCollector::onDiscoveryTimer()
{
	discover();
}

// ---------------------------------------------------------------------------
// Statistics tick
// ---------------------------------------------------------------------------
void StatsCollector::onStatsTimer()
{
	if (shutdown_)
		return;
	const uint64_t tickStart = os_gettime_ns();

	discover();

	OBSOutputAutoRelease primary = obs_frontend_get_streaming_output();
	const uint64_t now = os_gettime_ns();

	std::vector<OutputObservation> live;
	live.reserve(trackers_.size());
	size_t aitumCount = 0;

	for (auto it = trackers_.begin(); it != trackers_.end();) {
		OutputTracker &t = **it;
		// Drain BEFORE sampling: the sample then reflects at least the
		// state after every drained event (see SessionRegistry::reconcile).
		std::vector<OutputEvent> events = t.drainEvents();
		obs_output_t *output = t.acquire();
		if (!output) {
			t.detach();
			std::vector<OutputEvent> late = t.drainEvents();
			events.insert(events.end(), late.begin(), late.end());
			vanished_.push_back({t.instanceId(), std::move(events)});
			it = trackers_.erase(it);
			continue;
		}

		const OutputDescription d = describeOutput(output, output == primary.Get());
		OutputObservation obs;
		obs.instanceId = t.instanceId();
		obs.kind = d.kind;
		obs.outputName = d.outputName;
		obs.typeId = d.typeId;
		obs.destination = d.destination;
		obs.serviceName = d.serviceName;
		obs.ingestHost = d.ingestHost;
		obs.caps = d.caps;
		obs.firstSeenActive = t.firstSeenActive();
		obs.initialScan = t.initialScan();
		obs.events = std::move(events);
		obs.sample = sampleOutput(output, now);
		obs_output_release(output);

		if (obs.kind == ProviderKind::Aitum)
			aitumCount++;
		live.push_back(std::move(obs));
		++it;
	}

	registry_.update(now, live, vanished_);
	vanished_.clear();

	// Aitum destination list changes when outputs are started/stopped or
	// renamed; re-query on changes and periodically while visible.
	refreshAitumConfig(now, aitumCount != lastAitumTrackerCount_);
	lastAitumTrackerCount_ = aitumCount;
	lastCollectNs_ = os_gettime_ns() - tickStart;

	if (displayActive_ || debugTicks_)
		publish(now);

	const uint64_t elapsed = os_gettime_ns() - tickStart;
	tickCount_++;
	tickNsTotal_ += elapsed;
	if (elapsed > tickNsMax_)
		tickNsMax_ = elapsed;
}

void StatsCollector::refreshAitumConfig(uint64_t nowNs, bool force)
{
	if (shutdown_)
		return;
	if (!force && (!displayActive_ || nowNs - lastAitumQueryNs_ < kAitumRefreshNs))
		return;
	lastAitumQueryNs_ = nowNs;

	auto destinations = aitum_.queryDestinations();
	if (destinations) {
		registry_.setConfiguredDestinations(ProviderKind::Aitum, *destinations);
		aitumConfigKnown_ = true;
	} else if (aitumConfigKnown_) {
		registry_.forgetConfiguredDestinations(ProviderKind::Aitum);
		aitumConfigKnown_ = false;
	}
}

void StatsCollector::publish(uint64_t nowNs)
{
	if (shutdown_)
		return;
	DockSnapshot snap;
	snap.timestampNs = nowNs;
	// Every known row stays in the registry (restarts, ordering); only the
	// displayed ones reach the dock and the summary (core/aggregate.hpp).
	std::vector<RowSnapshot> all = registry_.snapshot(nowNs);
	snap.rows = displayedRows(all);
	if (const RowSnapshot *failure = lastFailure(all)) {
		snap.hasLastFailure = true;
		snap.lastFailure = *failure;
	}
	if (system_)
		snap.system = system_->sample(nowNs);
	snap.summary = computeSummary(snap.rows, snap.system);
	snap.aitumLoaded = aitum_.aitumLoaded();
	snap.aitumConfigAvailable = aitumConfigKnown_;
	last_ = std::move(snap);

	if (debugTicks_)
		logTick(all, last_);

	const uint64_t uiStart = os_gettime_ns();
	emit snapshotReady(last_); // dock updates synchronously (same thread)
	const uint64_t uiNs = os_gettime_ns() - uiStart;
	uiNsTotal_ += uiNs;
	uiCount_++;
	if (uiNs > uiNsMax_)
		uiNsMax_ = uiNs;
	if (debugTicks_)
		msd::log(LOG_INFO, "tick perf collect_us=%.1f ui_us=%.1f", (double)lastCollectNs_ / 1000.0,
			 (double)uiNs / 1000.0);
}

void StatsCollector::logTick(const std::vector<RowSnapshot> &all, const DockSnapshot &snap) const
{
	// Machine-readable lines for the isolated integration tests: every row the
	// registry knows, with shown=1 for the rows the dock displays.
	for (const RowSnapshot &r : all) {
		msd::log(LOG_INFO,
			 "tick row key=\"%s\" shown=%d state=%s health=%d present=%d kbps=%s avg=%s bytes=%s dropped=%s "
			 "total=%s congestion=%s connect=%s reconnects=%d duration_ms=%s start=%d code=%d",
			 r.key.c_str(), isRowDisplayed(r) ? 1 : 0, stateName(r.state), (int)r.health, r.objectPresent ? 1 : 0,
			 metricText(r.kbps).c_str(), metricText(r.avgKbps).c_str(), metricText(r.bytes).c_str(),
			 metricText(r.dropped).c_str(), metricText(r.totalFrames).c_str(),
			 metricText(r.congestion).c_str(), metricText(r.connectTimeMs).c_str(), r.reconnects,
			 r.durationNs.has() ? std::to_string(r.durationNs.value() / 1'000'000ULL).c_str() : "na",
			 (int)r.startSource, r.stopCode);
	}
	const SummarySnapshot &s = snap.summary;
	const SystemSnapshot &y = snap.system;
	msd::log(LOG_INFO,
		 "tick summary rows=%d running=%d live=%d connecting=%d reconnecting=%d failed=%d sum_kbps=%s "
		 "dropped=%s frames=%s overall=%d aitum_config=%d render_lagged=%s render_total=%s render_pct=%s "
		 "encode_skipped=%s encode_total=%s lag_window=%d last_failure=\"%s\"",
		 s.rows, s.running, s.live, s.connecting, s.reconnecting, s.failed, metricText(s.sumKbps).c_str(),
		 metricText(s.droppedSum).c_str(), metricText(s.framesSum).c_str(), (int)s.overall,
		 snap.aitumConfigAvailable ? 1 : 0, metricText(y.renderLagged).c_str(), metricText(y.renderTotal).c_str(),
		 metricText(y.renderLagPct).c_str(), metricText(y.encodeSkipped).c_str(), metricText(y.encodeTotal).c_str(),
		 (int)y.lagWindow, snap.hasLastFailure ? snap.lastFailure.key.c_str() : "none");
}

} // namespace msd
