// SPDX-License-Identifier: GPL-2.0-or-later
#include "session-registry.hpp"
#include "naming.hpp"

#include <algorithm>

namespace msd {

namespace {

int groupOf(ProviderKind kind)
{
	switch (kind) {
	case ProviderKind::Primary:
		return 0;
	case ProviderKind::Aitum:
		return 1;
	case ProviderKind::MultiRtmp:
		return 2;
	case ProviderKind::Generic:
	default:
		return 3;
	}
}

} // namespace

SessionRegistry::SessionRegistry()
{
	// The primary stream always has a row, even before OBS creates its output.
	Row &primary = ensureRow(primaryRowKey(), primaryRowKey(), 1, ProviderKind::Primary);
	primary.configured = true;
}

// ---------------------------------------------------------------------------
// Row lookup / creation
// ---------------------------------------------------------------------------
SessionRegistry::Row *SessionRegistry::find(const std::string &key)
{
	for (auto &r : rows_)
		if (r->key == key)
			return r.get();
	return nullptr;
}

const SessionRegistry::Row *SessionRegistry::find(const std::string &key) const
{
	for (const auto &r : rows_)
		if (r->key == key)
			return r.get();
	return nullptr;
}

SessionRegistry::Row &SessionRegistry::ensureRow(const std::string &key, const std::string &baseKey, int ordinal,
						 ProviderKind kind)
{
	if (Row *existing = find(key))
		return *existing;
	auto row = std::make_unique<Row>();
	row->key = key;
	row->baseKey = baseKey;
	row->ordinal = ordinal;
	row->kind = kind;
	row->order = nextOrder_++;
	rows_.push_back(std::move(row));
	return *rows_.back();
}

// Maps an output object to its row. The binding is fixed for the lifetime of
// the object unless its classification changes (e.g. OBS replaced its primary
// stream output and the old object is now just a generic output).
SessionRegistry::Row *SessionRegistry::bind(const OutputObservation &obs)
{
	const std::string base = baseRowKey(obs);

	auto it = bindings_.find(obs.instanceId);
	if (it != bindings_.end()) {
		if (it->second.baseKey == base) {
			if (Row *row = find(it->second.key))
				return row;
		}
		// Classification changed or row vanished: detach and re-bind.
		if (Row *old = find(it->second.key); old && old->liveInstance == obs.instanceId) {
			old->liveInstance = 0;
			old->signalsObserved = false;
		}
		bindings_.erase(it);
	}

	for (int n = 1;; ++n) {
		const std::string key = n == 1 ? base : base + "#" + std::to_string(n);
		Row *row = find(key);
		if (!row || row->liveInstance == 0 || row->liveInstance == obs.instanceId) {
			Row &r = ensureRow(key, base, n, obs.kind);
			bindings_[obs.instanceId] = {key, base};
			return &r;
		}
	}
}

void SessionRegistry::detachInstance(uint64_t instanceId, uint64_t nowNs)
{
	auto it = bindings_.find(instanceId);
	if (it == bindings_.end())
		return;
	if (Row *row = find(it->second.key); row && row->liveInstance == instanceId) {
		row->liveInstance = 0;
		row->signalsObserved = false;
		// Object destroyed without a "stop" signal (should not normally happen).
		if (row->session.exists && isRunning(row->session.state))
			endSession(*row, nowNs, StopCode::Success, {}, true, false);
	}
	bindings_.erase(it);
}

// ---------------------------------------------------------------------------
// Session state machine
// ---------------------------------------------------------------------------
void SessionRegistry::beginSession(Row &row, uint64_t ts, StartSource source)
{
	row.session = Session{};
	row.session.exists = true;
	row.session.state = OutputState::Connecting;
	row.session.beginNs = ts;
	row.session.startSource = source;
}

void SessionRegistry::endSession(Row &row, uint64_t ts, int code, const std::string &err, bool unknownReason,
				 bool bySignal)
{
	Session &s = row.session;
	s.state = unknownReason ? OutputState::Stopped : stateForStopCode(code);
	s.endNs = ts;
	s.stopCode = unknownReason ? 0 : code;
	s.lastError = err;
	s.endReasonUnknown = unknownReason;
	s.endedBySignal = bySignal;
	s.inactiveTicks = 0;
	s.meter.reset();
	s.congestion.reset();
	s.recent.reset();
}

void SessionRegistry::applyEvent(Row &row, const OutputEvent &e)
{
	Session &s = row.session;
	const bool running = s.exists && isRunning(s.state);

	switch (e.type) {
	case OutputEventType::Starting:
	case OutputEventType::Activate:
		if (!running)
			beginSession(row, e.timestampNs, StartSource::None);
		break;

	case OutputEventType::Start:
		if (!running)
			beginSession(row, e.timestampNs, StartSource::None);
		row.session.state = OutputState::Live;
		if (row.session.startSource != StartSource::Signal) {
			row.session.startNs = e.timestampNs;
			row.session.startSource = StartSource::Signal;
		}
		row.session.lastConnectedNs = e.timestampNs;
		row.session.inactiveTicks = 0;
		break;

	case OutputEventType::ReconnectSuccess:
		if (!running)
			beginSession(row, e.timestampNs, StartSource::Unknown);
		row.session.state = OutputState::Live;
		row.session.lastConnectedNs = e.timestampNs;
		row.session.inactiveTicks = 0;
		break;

	case OutputEventType::Reconnect:
		if (running) {
			s.state = OutputState::Reconnecting;
			s.reconnects++;
		}
		break;

	case OutputEventType::Stopping:
		if (running)
			s.state = OutputState::Stopping;
		break;

	case OutputEventType::Stop:
		if (e.hasFinalCounters && running)
			absorbCounters(row, e.timestampNs, e.finalBytes, e.finalDropped, e.finalTotalFrames);
		if (running) {
			endSession(row, e.timestampNs, e.stopCode, e.lastError, false, true);
		} else if (s.exists && s.endReasonUnknown) {
			// Polling ended the session first; the signal refines the reason.
			s.state = stateForStopCode(e.stopCode);
			s.stopCode = e.stopCode;
			s.lastError = e.lastError;
			s.endReasonUnknown = false;
			s.endedBySignal = true;
		} else if (!s.exists) {
			// Failed before we ever saw it start (e.g. immediate connect error).
			beginSession(row, e.timestampNs, StartSource::None);
			endSession(row, e.timestampNs, e.stopCode, e.lastError, false, true);
		}
		break;

	case OutputEventType::Deactivate:
	default:
		break;
	}
}

void SessionRegistry::absorbCounters(Row &row, uint64_t ts, uint64_t bytes, int dropped, int total)
{
	Session &s = row.session;
	const uint64_t sessionBytes = s.bytes.update(bytes);
	s.total.update(total > 0 ? (uint64_t)total : 0);
	if (row.caps.droppedFrames || dropped > 0 || s.sawDrops) {
		if (dropped > 0)
			s.sawDrops = true;
		s.dropped.update(dropped > 0 ? (uint64_t)dropped : 0);
		s.recent.addSample(ts, s.dropped.total(), s.total.total());
	}
	s.meter.addSample(ts, sessionBytes);
}

void SessionRegistry::reconcile(Row &row, const OutputObservation &obs, uint64_t nowNs)
{
	const OutputSample &smp = obs.sample;
	Session &s = row.session;
	const bool running = s.exists && isRunning(s.state);

	if (smp.active) {
		if (!running) {
			// Polling may only start a session if no signal has been seen
			// for this object; otherwise an output that is still winding
			// down right after its "stop" signal would look restarted.
			if (row.signalsObserved)
				return;
			const StartSource src = (obs.initialScan && obs.firstSeenActive) ? StartSource::Unknown
											  : StartSource::Detected;
			beginSession(row, nowNs, src);
			row.session.state = smp.reconnecting ? OutputState::Reconnecting : OutputState::Live;
			if (src == StartSource::Detected)
				row.session.startNs = nowNs;
			if (!smp.reconnecting)
				row.session.lastConnectedNs = src == StartSource::Detected ? nowNs : 0;
		} else if (smp.reconnecting) {
			s.state = OutputState::Reconnecting;
		} else if (s.state == OutputState::Connecting || s.state == OutputState::Reconnecting) {
			// Connected but the "start"/"reconnect_success" signal is still in flight.
			s.state = OutputState::Live;
			if (s.startNs == 0 && s.startSource != StartSource::Unknown) {
				s.startNs = nowNs;
				s.startSource = StartSource::Detected;
			}
			if (s.lastConnectedNs == 0)
				s.lastConnectedNs = nowNs;
		}
		row.session.inactiveTicks = 0;
		absorbCounters(row, nowNs, smp.totalBytes, smp.framesDropped, smp.totalFrames);
		if (row.caps.congestion || smp.congestion > 0.0f || row.session.sawCongestion) {
			if (smp.congestion > 0.0f)
				row.session.sawCongestion = true;
			row.session.congestion.addSample(smp.congestion);
		}
		return;
	}

	// Not active.
	if (!running)
		return;
	if (s.state == OutputState::Connecting) {
		// Outputs are inactive while connecting. An *inferred* connection
		// (no signal ever seen) that shows no activity for too long is
		// not a connection attempt: fall back to "not started".
		if (s.inferredConnecting && !row.signalsObserved && nowNs - s.beginNs >= kInferredConnectTimeoutNs)
			row.session = Session{};
		return;
	}
	if (++s.inactiveTicks >= kInactiveGraceTicks)
		endSession(row, nowNs, StopCode::Success, {}, true, false);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void SessionRegistry::update(uint64_t nowNs, const std::vector<OutputObservation> &live,
			     const std::vector<VanishedOutput> &vanished)
{
	// 1. Objects that were destroyed: apply their last events, then detach.
	for (const VanishedOutput &v : vanished) {
		auto it = bindings_.find(v.instanceId);
		if (it == bindings_.end())
			continue;
		if (Row *row = find(it->second.key)) {
			for (const OutputEvent &e : v.events)
				applyEvent(*row, e);
		}
		detachInstance(v.instanceId, nowNs);
	}

	// 2. Live objects.
	for (const OutputObservation &obs : live) {
		Row *row = bind(obs);
		if (!row)
			continue;

		row->kind = obs.kind;
		row->outputName = obs.outputName;
		row->typeId = obs.typeId;
		if (!obs.destination.empty())
			row->destination = obs.destination;
		row->serviceName = obs.serviceName;
		if (!obs.ingestHost.empty())
			row->ingestHost = obs.ingestHost;
		row->caps = obs.caps;

		if (row->liveInstance != obs.instanceId) {
			// A different object now backs this row (e.g. Aitum restarted
			// the destination). Close any session of the previous object.
			if (row->session.exists && isRunning(row->session.state))
				endSession(*row, nowNs, StopCode::Success, {}, true, false);
			row->liveInstance = obs.instanceId;
			row->signalsObserved = false;

			// Aitum only creates an output object when a destination is
			// started and releases it on stop (FINDINGS.md §2), so a new,
			// not-yet-active Aitum object is a connection in progress.
			const bool hasStop = std::any_of(obs.events.begin(), obs.events.end(), [](const OutputEvent &e) {
				return e.type == OutputEventType::Stop;
			});
			if (obs.kind == ProviderKind::Aitum && !obs.sample.active && !hasStop) {
				beginSession(*row, nowNs, StartSource::None);
				row->session.inferredConnecting = true;
			}
		}

		for (const OutputEvent &e : obs.events) {
			applyEvent(*row, e);
			row->signalsObserved = true;
		}

		reconcile(*row, obs, nowNs);
		row->lastSample = obs.sample;
		row->haveSample = true;
	}

	prune();
}

// ---------------------------------------------------------------------------
// Configuration & housekeeping
// ---------------------------------------------------------------------------
void SessionRegistry::setConfiguredDestinations(ProviderKind kind, const std::vector<ConfiguredDestination> &dests)
{
	for (auto &r : rows_) {
		if (r->kind == kind && r->kind != ProviderKind::Primary) {
			r->configured = false;
			r->configIndex = -1;
		}
	}
	int index = 0;
	for (const ConfiguredDestination &d : dests) {
		if (d.kind != kind || d.name.empty())
			continue;
		const std::string key = kind == ProviderKind::Aitum ? aitumRowKey(d.name) : d.name;
		Row &row = ensureRow(key, key, 1, kind);
		row.destination = d.name;
		row.vertical = d.vertical;
		row.configured = true;
		row.configIndex = index++;
	}
	if (kind == ProviderKind::Aitum)
		aitumConfigKnown_ = true;
	prune();
}

void SessionRegistry::forgetConfiguredDestinations(ProviderKind kind)
{
	for (auto &r : rows_) {
		if (r->kind == kind && r->kind != ProviderKind::Primary) {
			// Keep rows, but they are now retained like unconfigured rows.
			r->configured = false;
			r->configIndex = -1;
		}
	}
	if (kind == ProviderKind::Aitum)
		aitumConfigKnown_ = false;
}

void SessionRegistry::prune()
{
	// Aitum destinations that are neither configured nor live are gone
	// (renamed / deleted) once the configuration is known.
	rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
				   [this](const std::unique_ptr<Row> &r) {
					   return r->kind == ProviderKind::Aitum && aitumConfigKnown_ &&
						  !r->configured && r->liveInstance == 0;
				   }),
		    rows_.end());

	// Bound the number of retained finished, unconfigured rows.
	std::vector<Row *> ended;
	for (auto &r : rows_) {
		if (!r->configured && r->kind != ProviderKind::Primary && r->liveInstance == 0)
			ended.push_back(r.get());
	}
	if (ended.size() > kMaxRetainedEndedRows) {
		std::sort(ended.begin(), ended.end(),
			  [](const Row *a, const Row *b) { return a->session.endNs < b->session.endNs; });
		const size_t excess = ended.size() - kMaxRetainedEndedRows;
		for (size_t i = 0; i < excess; ++i) {
			const Row *victim = ended[i];
			rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
						   [victim](const std::unique_ptr<Row> &r) { return r.get() == victim; }),
				    rows_.end());
		}
	}
}

void SessionRegistry::resetStatistics()
{
	for (auto &r : rows_) {
		Session &s = r->session;
		if (!s.exists)
			continue;
		s.droppedBaseline = s.dropped.total();
		s.totalBaseline = s.total.total();
		s.sinceReset = true;
		s.recent.reset();
	}
}

void SessionRegistry::clearEnded()
{
	rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
				   [](const std::unique_ptr<Row> &r) {
					   const bool running = r->session.exists && isRunning(r->session.state);
					   return !running && !r->configured && r->kind != ProviderKind::Primary &&
						  r->liveInstance == 0;
				   }),
		    rows_.end());
	for (auto &r : rows_) {
		if (r->session.exists && !isRunning(r->session.state))
			r->session = Session{};
	}
	// Bindings to removed rows are recreated on the next observation.
	for (auto it = bindings_.begin(); it != bindings_.end();) {
		if (!find(it->second.key))
			it = bindings_.erase(it);
		else
			++it;
	}
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------
RowSnapshot SessionRegistry::makeSnapshot(const Row &row, uint64_t nowNs) const
{
	RowSnapshot out;
	const Session &s = row.session;
	const bool running = s.exists && isRunning(s.state);
	const bool flowing = running && (s.state == OutputState::Live || s.state == OutputState::Reconnecting ||
					 s.state == OutputState::Stopping);

	out.key = row.key;
	out.kind = row.kind;
	out.destination = row.destination;
	out.outputName = row.outputName;
	out.typeId = row.typeId;
	out.serviceName = row.serviceName;
	out.ingestHost = row.ingestHost;
	out.ordinal = row.ordinal;
	out.vertical = row.vertical;
	out.configured = row.configured;
	out.objectPresent = row.liveInstance != 0;
	out.everSampled = row.haveSample;

	out.hasSession = s.exists;
	out.state = s.exists ? s.state : OutputState::Idle;
	out.stopCode = s.stopCode;
	out.lastError = s.lastError;
	out.endReasonUnknown = s.endReasonUnknown;
	out.endNs = s.exists && !running ? s.endNs : 0;
	out.reconnects = s.reconnects;
	out.startSource = s.startSource;
	out.countsSinceReset = s.sinceReset;

	const Unavailable inactiveReason = Unavailable::NotActive;

	// Duration (from the moment it went live).
	if (!s.exists) {
		out.durationNs = Metric<uint64_t>::na(Unavailable::NotActive);
	} else if (s.startSource == StartSource::Unknown) {
		out.durationNs = Metric<uint64_t>::na(Unavailable::Unknown);
	} else if (s.startNs == 0) {
		out.durationNs = Metric<uint64_t>::na(running ? Unavailable::NotMeasured : Unavailable::NotActive);
	} else {
		const uint64_t end = running ? nowNs : s.endNs;
		out.durationNs = Metric<uint64_t>::of(end > s.startNs ? end - s.startNs : 0);
	}

	if (s.lastConnectedNs != 0 && nowNs >= s.lastConnectedNs)
		out.lastConnectedAgoNs = Metric<uint64_t>::of(nowNs - s.lastConnectedNs);
	else
		out.lastConnectedAgoNs = Metric<uint64_t>::na(s.exists ? Unavailable::Unknown : Unavailable::NotActive);

	// Bitrate.
	out.kbps = flowing ? s.meter.currentKbps() : Metric<double>::na(inactiveReason);
	if (!s.exists || !s.bytes.hasValue()) {
		out.avgKbps = Metric<double>::na(running ? Unavailable::NotMeasured : inactiveReason);
	} else if (s.startSource == StartSource::Unknown) {
		out.avgKbps = Metric<double>::na(Unavailable::Unknown);
	} else if (s.startNs == 0) {
		out.avgKbps = Metric<double>::na(Unavailable::NotMeasured);
	} else {
		const uint64_t end = running ? nowNs : s.endNs;
		out.avgKbps = averageKbps(s.bytes.total(), end > s.startNs ? end - s.startNs : 0);
	}
	out.bytes = s.exists && s.bytes.hasValue() ? Metric<uint64_t>::of(s.bytes.total())
						   : Metric<uint64_t>::na(running ? Unavailable::NotMeasured : inactiveReason);

	// Frames.
	const bool dropsSupported = row.caps.droppedFrames || s.sawDrops;
	if (s.exists && s.total.hasValue()) {
		const uint64_t total = s.total.total() - std::min(s.totalBaseline, s.total.total());
		out.totalFrames = Metric<uint64_t>::of(total);
		if (dropsSupported && s.dropped.hasValue()) {
			const uint64_t dropped = s.dropped.total() - std::min(s.droppedBaseline, s.dropped.total());
			out.dropped = Metric<uint64_t>::of(dropped);
			out.dropPct = total > 0 ? Metric<double>::of((double)dropped * 100.0 / (double)total)
						: Metric<double>::na(Unavailable::NotMeasured);
		} else {
			out.dropped = Metric<uint64_t>::na(dropsSupported ? Unavailable::NotMeasured
									   : Unavailable::NotSupported);
			out.dropPct = Metric<double>::na(out.dropped.reason());
		}
	} else {
		const Unavailable r = running ? Unavailable::NotMeasured : inactiveReason;
		out.totalFrames = Metric<uint64_t>::na(r);
		out.dropped = Metric<uint64_t>::na(dropsSupported || !row.haveSample ? r : Unavailable::NotSupported);
		out.dropPct = Metric<double>::na(out.dropped.reason());
	}
	out.recentDropPct = flowing && dropsSupported ? s.recent.recentPercent()
						      : Metric<double>::na(dropsSupported ? inactiveReason
											  : Unavailable::NotSupported);
	out.dropHealth = worst(percentHealth(out.dropPct), percentHealth(out.recentDropPct));

	// Congestion.
	const bool congestionSupported = row.caps.congestion || s.sawCongestion;
	if (!congestionSupported && row.haveSample)
		out.congestion = Metric<double>::na(Unavailable::NotSupported);
	else
		out.congestion = flowing ? s.congestion.value() : Metric<double>::na(inactiveReason);
	out.congestionHealth = congestionHealth(out.congestion);

	// Connection (handshake) time, last known value. The counter is 0 both
	// before a connection and after a sub-millisecond one (loopback), so a 0
	// is only trusted once this session has actually connected.
	const int connectMs = row.haveSample ? row.lastSample.connectTimeMs : -1;
	const bool connected = s.exists && s.lastConnectedNs != 0;
	if (row.haveSample && connectMs < 0 && !row.caps.connectTime)
		out.connectTimeMs = Metric<int>::na(Unavailable::NotSupported);
	else if (connectMs > 0 || (connectMs == 0 && connected && row.caps.connectTime))
		out.connectTimeMs = Metric<int>::of(connectMs);
	else
		out.connectTimeMs = Metric<int>::na(running ? Unavailable::NotMeasured : inactiveReason);

	// Video / encoder (static info of the current or last object).
	if (row.haveSample && row.lastSample.width > 0 && row.lastSample.height > 0) {
		out.width = Metric<uint32_t>::of(row.lastSample.width);
		out.height = Metric<uint32_t>::of(row.lastSample.height);
	} else {
		out.width = Metric<uint32_t>::na(row.haveSample ? Unavailable::Unknown : Unavailable::NotActive);
		out.height = out.width;
	}
	if (row.haveSample) {
		out.encoder = row.lastSample.encoder;
		out.delaySec = row.lastSample.delaySec;
	}
	out.fps = out.encoder.fps > 0.0 ? Metric<double>::of(out.encoder.fps)
					: Metric<double>::na(row.haveSample ? Unavailable::Unknown : Unavailable::NotActive);
	if (out.encoder.dedicatedPipeline && out.encoder.pipelineTotal > 0)
		out.pipelineSkipPct = Metric<double>::of((double)out.encoder.pipelineSkipped * 100.0 /
							 (double)out.encoder.pipelineTotal);
	else
		out.pipelineSkipPct = Metric<double>::na(out.encoder.dedicatedPipeline ? Unavailable::NotMeasured
										: Unavailable::NotSupported);
	out.pipelineHealth = flowing ? percentHealth(out.pipelineSkipPct) : Health::Inactive;

	// Overall row health.
	out.health = stateHealth(out.state);
	if (out.state == OutputState::Live) {
		out.health = worst(out.health, out.dropHealth);
		out.health = worst(out.health, out.congestionHealth);
		out.health = worst(out.health, out.pipelineHealth);
	}
	return out;
}

std::vector<RowSnapshot> SessionRegistry::snapshot(uint64_t nowNs) const
{
	std::vector<const Row *> ordered;
	ordered.reserve(rows_.size());
	for (const auto &r : rows_)
		ordered.push_back(r.get());

	std::stable_sort(ordered.begin(), ordered.end(), [](const Row *a, const Row *b) {
		const int ga = groupOf(a->kind), gb = groupOf(b->kind);
		if (ga != gb)
			return ga < gb;
		const bool ca = a->configIndex >= 0, cb = b->configIndex >= 0;
		if (ca != cb)
			return ca; // configured destinations first, in config order
		if (ca && cb && a->configIndex != b->configIndex)
			return a->configIndex < b->configIndex;
		return a->order < b->order;
	});

	std::vector<RowSnapshot> out;
	out.reserve(ordered.size());
	for (const Row *r : ordered)
		out.push_back(makeSnapshot(*r, nowNs));
	return out;
}

} // namespace msd
