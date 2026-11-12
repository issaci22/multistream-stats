// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <core/naming.hpp>
#include <core/observation.hpp>
#include <core/session-registry.hpp>

#include <optional>
#include <string>
#include <vector>

namespace msdtest {

constexpr uint64_t kSec = 1'000'000'000ULL;

struct Clock {
	uint64_t now = 100 * kSec;
	uint64_t advance(double seconds = 1.0)
	{
		now += (uint64_t)(seconds * (double)kSec);
		return now;
	}
};

// A simulated libobs output object.
struct FakeOutput {
	uint64_t id = 0;
	msd::ProviderKind kind = msd::ProviderKind::Generic;
	std::string name;
	std::string type = "rtmp_output";
	std::string destination;
	std::string host = "ingest.example.com";
	bool active = false;
	bool reconnecting = false;
	uint64_t bytes = 0;
	int dropped = 0;
	int total = 0;
	float congestion = 0.0f;
	int connectMs = -1;
	bool firstSeenActive = false;
	bool initialScan = false;
	std::vector<msd::OutputEvent> pending;

	void event(msd::OutputEventType t, uint64_t ts, int code = 0, std::string err = {})
	{
		msd::OutputEvent e;
		e.type = t;
		e.timestampNs = ts;
		e.stopCode = code;
		e.lastError = std::move(err);
		pending.push_back(e);
	}

	msd::OutputObservation observe(uint64_t ts)
	{
		msd::OutputObservation o;
		o.instanceId = id;
		o.kind = kind;
		o.outputName = name;
		o.typeId = type;
		o.destination = destination;
		o.ingestHost = host;
		o.caps = msd::capabilitiesForOutputType(type);
		o.firstSeenActive = firstSeenActive;
		o.initialScan = initialScan;
		o.events = pending;
		pending.clear();
		o.sample.timestampNs = ts;
		o.sample.active = active;
		o.sample.reconnecting = reconnecting;
		o.sample.totalBytes = bytes;
		o.sample.framesDropped = dropped;
		o.sample.totalFrames = total;
		o.sample.congestion = congestion;
		o.sample.connectTimeMs = connectMs;
		o.sample.width = 1920;
		o.sample.height = 1080;
		o.sample.encoder.present = true;
		o.sample.encoder.fps = 60.0;
		return o;
	}
};

inline FakeOutput aitumOutput(uint64_t id, const std::string &dest)
{
	FakeOutput f;
	f.id = id;
	f.kind = msd::ProviderKind::Aitum;
	f.name = std::string(msd::kAitumOutputPrefix) + dest;
	f.destination = dest;
	return f;
}

// Pointer into a vector the caller keeps alive (never pass a temporary).
inline const msd::RowSnapshot *findRow(const std::vector<msd::RowSnapshot> &rows, const std::string &key)
{
	for (const auto &r : rows)
		if (r.key == key)
			return &r;
	return nullptr;
}

// Copy of one row from a fresh snapshot (safe to keep).
inline std::optional<msd::RowSnapshot> rowOf(const msd::SessionRegistry &reg, uint64_t now, const std::string &key)
{
	for (const auto &r : reg.snapshot(now))
		if (r.key == key)
			return r;
	return std::nullopt;
}

} // namespace msdtest
