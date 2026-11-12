// SPDX-License-Identifier: GPL-2.0-or-later
#include "output-tracker.hpp"

#include <util/platform.h>

namespace msd {

const OutputTracker::Connection OutputTracker::kConnections[] = {
	{"starting", &OutputTracker::onStarting},
	{"activate", &OutputTracker::onActivate},
	{"start", &OutputTracker::onStart},
	{"stopping", &OutputTracker::onStopping},
	{"stop", &OutputTracker::onStop},
	{"reconnect", &OutputTracker::onReconnect},
	{"reconnect_success", &OutputTracker::onReconnectSuccess},
	{"deactivate", &OutputTracker::onDeactivate},
};

OutputTracker::OutputTracker(uint64_t instanceId, obs_output_t *output, bool initialScan)
	: instanceId_(instanceId),
	  initialScan_(initialScan)
{
	identity_ = output;
	weak_ = obs_output_get_weak_output(output);
	connectSignals(output);
	// Sampled *after* connecting, so a start that happens in between is
	// either seen as a signal or as already-active - never lost.
	firstSeenActive_ = obs_output_active(output);
}

OutputTracker::~OutputTracker()
{
	disconnectSignals();
	if (weak_) {
		obs_weak_output_release(weak_);
		weak_ = nullptr;
	}
}

void OutputTracker::connectSignals(obs_output_t *output)
{
	handler_ = obs_output_get_signal_handler(output);
	if (!handler_)
		return;
	for (const Connection &c : kConnections)
		signal_handler_connect_ref(handler_, c.signal, c.callback, this);
	connected_ = true;
}

void OutputTracker::disconnectSignals()
{
	if (!connected_ || !handler_)
		return;
	// Each disconnect drops the reference taken by connect_ref; the handler
	// is freed with the last one if the output is already gone.
	for (const Connection &c : kConnections)
		signal_handler_disconnect(handler_, c.signal, c.callback, this);
	connected_ = false;
	handler_ = nullptr;
}

obs_output_t *OutputTracker::acquire() const
{
	return weak_ ? obs_weak_output_get_output(weak_) : nullptr;
}

std::vector<OutputEvent> OutputTracker::drainEvents()
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<OutputEvent> out;
	out.swap(events_);
	return out;
}

void OutputTracker::pushEvent(OutputEvent e)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (events_.size() >= kMaxQueuedEvents)
		events_.erase(events_.begin()); // pathological flapping: keep newest
	events_.push_back(std::move(e));
}

void OutputTracker::pushSimple(OutputEventType type)
{
	OutputEvent e;
	e.type = type;
	e.timestampNs = os_gettime_ns();
	pushEvent(std::move(e));
}

// --- signal callbacks (libobs threads) -------------------------------------
void OutputTracker::onStarting(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::Starting);
}

void OutputTracker::onActivate(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::Activate);
}

void OutputTracker::onStart(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::Start);
}

void OutputTracker::onStopping(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::Stopping);
}

void OutputTracker::onReconnect(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::Reconnect);
}

void OutputTracker::onReconnectSuccess(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::ReconnectSuccess);
}

void OutputTracker::onDeactivate(void *data, calldata_t *)
{
	static_cast<OutputTracker *>(data)->pushSimple(OutputEventType::Deactivate);
}

void OutputTracker::onStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<OutputTracker *>(data);
	OutputEvent e;
	e.type = OutputEventType::Stop;
	e.timestampNs = os_gettime_ns();
	e.stopCode = (int)calldata_int(cd, "code");

	// The emitting output is alive for the duration of its own signal, so
	// its final counters and error message can be read here. Aitum releases
	// the object right after this signal, so this is the last chance.
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	if (output) {
		if (const char *err = obs_output_get_last_error(output))
			e.lastError = err;
		e.finalBytes = obs_output_get_total_bytes(output);
		e.finalDropped = obs_output_get_frames_dropped(output);
		e.finalTotalFrames = obs_output_get_total_frames(output);
		e.hasFinalCounters = true;
	}
	self->pushEvent(std::move(e));
}

} // namespace msd
