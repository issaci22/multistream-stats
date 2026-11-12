// SPDX-License-Identifier: GPL-2.0-or-later
//
// OutputTracker observes one libobs output object WITHOUT owning it:
//  * it holds only a weak reference, so it never extends the lifetime of an
//    output owned by OBS or another plugin (Aitum releases its outputs on stop);
//  * output signals are connected with signal_handler_connect_ref, which keeps
//    the *signal handler* alive after the output is destroyed, so the later
//    disconnect is always safe (same pattern as OBSSignal);
//  * signal callbacks run on libobs threads and only timestamp + enqueue an
//    event under a small mutex; they never block and never touch Qt.
//
// Lifetime: created/destroyed on the UI thread. The destructor disconnects
// every signal; signal_handler_disconnect waits for an in-flight callback of
// that signal to return, so no callback can run after destruction.
#pragma once

#include <core/observation.hpp>

#include <obs.h>

#include <mutex>
#include <vector>

namespace msd {

class OutputTracker {
public:
	OutputTracker(uint64_t instanceId, obs_output_t *output, bool initialScan);
	~OutputTracker();

	OutputTracker(const OutputTracker &) = delete;
	OutputTracker &operator=(const OutputTracker &) = delete;

	uint64_t instanceId() const { return instanceId_; }
	bool initialScan() const { return initialScan_; }
	bool firstSeenActive() const { return firstSeenActive_; }

	// Identity only - never dereferenced. Used to skip already-tracked
	// outputs during obs_enum_outputs without taking references.
	const void *identity() const { return identity_; }

	// Strong reference (caller must obs_output_release) or nullptr if the
	// output has been destroyed.
	obs_output_t *acquire() const;

	std::vector<OutputEvent> drainEvents();

	// Disconnect all signals now (waits for in-flight callbacks). Used before
	// draining the final events of an output that has been destroyed, so no
	// late "stop" can be lost. Idempotent; also done by the destructor.
	void detach() { disconnectSignals(); }

	// Inject an event observed through another channel (frontend events).
	void pushEvent(OutputEvent e);

private:
	static void onStarting(void *data, calldata_t *cd);
	static void onActivate(void *data, calldata_t *cd);
	static void onStart(void *data, calldata_t *cd);
	static void onStopping(void *data, calldata_t *cd);
	static void onStop(void *data, calldata_t *cd);
	static void onReconnect(void *data, calldata_t *cd);
	static void onReconnectSuccess(void *data, calldata_t *cd);
	static void onDeactivate(void *data, calldata_t *cd);

	void pushSimple(OutputEventType type);
	void connectSignals(obs_output_t *output);
	void disconnectSignals();

	struct Connection {
		const char *signal;
		signal_callback_t callback;
	};
	static const Connection kConnections[];

	const uint64_t instanceId_;
	const bool initialScan_;
	bool firstSeenActive_ = false;
	const void *identity_ = nullptr;
	obs_weak_output_t *weak_ = nullptr;
	signal_handler_t *handler_ = nullptr; // kept alive by connect_ref
	bool connected_ = false;

	std::mutex mutex_;
	std::vector<OutputEvent> events_;
	static constexpr size_t kMaxQueuedEvents = 256;
};

} // namespace msd
