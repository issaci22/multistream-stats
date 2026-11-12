// SPDX-License-Identifier: GPL-2.0-or-later
//
// "Collector-level" tests: the SessionRegistry is driven with synthetic
// observations exactly as the libobs adapter produces them each tick.
#include <doctest.h>

#include "test-helpers.hpp"

#include <core/aggregate.hpp>

using namespace msd;
using namespace msdtest;

namespace {

void tick(SessionRegistry &reg, Clock &clk, std::vector<FakeOutput *> live, std::vector<VanishedOutput> gone = {},
	  double seconds = 1.0)
{
	clk.advance(seconds);
	std::vector<OutputObservation> obs;
	for (FakeOutput *f : live)
		obs.push_back(f->observe(clk.now));
	reg.update(clk.now, obs, gone);
}

VanishedOutput vanish(FakeOutput &f)
{
	VanishedOutput v;
	v.instanceId = f.id;
	v.events = f.pending;
	f.pending.clear();
	return v;
}

} // namespace

TEST_CASE("Primary row always exists and reports N/A, not zero, when idle")
{
	SessionRegistry reg;
	auto rows = reg.snapshot(1);
	REQUIRE(rows.size() == 1);
	const RowSnapshot &p = rows[0];
	CHECK(p.key == "primary");
	CHECK(p.kind == ProviderKind::Primary);
	CHECK(p.state == OutputState::Idle);
	CHECK(p.health == Health::Inactive);
	CHECK_FALSE(p.kbps.has());
	CHECK(p.kbps.reason() == Unavailable::NotActive);
	CHECK_FALSE(p.dropped.has());
	CHECK_FALSE(p.durationNs.has());
	CHECK_FALSE(p.bytes.has());
}

TEST_CASE("Aitum destination lifecycle: configured -> connecting -> live -> stopped -> restarted")
{
	SessionRegistry reg;
	Clock clk;
	reg.setConfiguredDestinations(ProviderKind::Aitum, {{ProviderKind::Aitum, "YouTube Horizontal", false},
							    {ProviderKind::Aitum, "Kick Output", false}});

	auto rows = reg.snapshot(clk.now);
	REQUIRE(rows.size() == 3);
	CHECK(rows[1].key == "aitum:YouTube Horizontal"); // config order preserved
	CHECK(rows[2].key == "aitum:Kick Output");
	CHECK(rows[1].state == OutputState::Idle);
	CHECK_FALSE(rows[1].objectPresent);

	// Aitum creates the output object when "start" is clicked; it is not
	// active until the RTMP connection succeeds.
	FakeOutput yt = aitumOutput(10, "YouTube Horizontal");
	tick(reg, clk, {&yt});
	auto r = rowOf(reg, clk.now,"aitum:YouTube Horizontal");
	REQUIRE(r);
	CHECK(r->objectPresent);
	CHECK(r->state == OutputState::Connecting);
	CHECK(r->health == Health::Warning);

	// Connected.
	const uint64_t startTs = clk.now + 300'000'000ULL;
	yt.event(OutputEventType::Activate, startTs);
	yt.event(OutputEventType::Start, startTs);
	yt.active = true;
	yt.connectMs = 290;
	tick(reg, clk, {&yt});
	r = rowOf(reg, clk.now,"aitum:YouTube Horizontal");
	CHECK(r->state == OutputState::Live);
	CHECK(r->startSource == StartSource::Signal);
	CHECK(r->health == Health::Good);
	CHECK(r->connectTimeMs.has());
	CHECK(r->connectTimeMs.value() == 290);
	CHECK_FALSE(r->kbps.has()); // only one byte sample so far

	for (int i = 0; i < 10; ++i) {
		yt.bytes += 1'250'000; // 10,000 kb/s
		yt.total += 60;
		tick(reg, clk, {&yt});
	}
	r = rowOf(reg, clk.now,"aitum:YouTube Horizontal");
	REQUIRE(r->kbps.has());
	CHECK(r->kbps.value() == doctest::Approx(10000.0));
	REQUIRE(r->durationNs.has());
	CHECK(r->durationNs.value() == clk.now - startTs);
	REQUIRE(r->dropped.has());
	CHECK(r->dropped.value() == 0); // genuine zero: rtmp_output reports drops
	CHECK(r->totalFrames.value() == 600);

	// User stops: the "stop" signal arrives, then Aitum releases the object.
	yt.dropped = 7;
	yt.event(OutputEventType::Stopping, clk.now + 1);
	OutputEvent stop;
	stop.type = OutputEventType::Stop;
	stop.timestampNs = clk.now + 2;
	stop.stopCode = StopCode::Success;
	stop.hasFinalCounters = true;
	stop.finalBytes = yt.bytes + 1000;
	stop.finalDropped = 7;
	stop.finalTotalFrames = yt.total + 3;
	yt.pending.push_back(stop);
	tick(reg, clk, {}, {vanish(yt)});

	r = rowOf(reg, clk.now,"aitum:YouTube Horizontal");
	REQUIRE(r);
	CHECK_FALSE(r->objectPresent);
	CHECK(r->state == OutputState::Stopped);
	CHECK(r->health == Health::Inactive);
	CHECK_FALSE(r->kbps.has()); // no "current" bitrate after stop
	CHECK(r->bytes.value() == yt.bytes + 1000); // final counters from the stop callback
	CHECK(r->dropped.value() == 7);
	CHECK(r->totalFrames.value() == 603);
	const uint64_t finalDuration = r->durationNs.value();
	clk.advance(30);
	CHECK(rowOf(reg, clk.now,"aitum:YouTube Horizontal")->durationNs.value() == finalDuration);

	// Restart: a NEW object for the same destination -> fresh session.
	FakeOutput yt2 = aitumOutput(11, "YouTube Horizontal");
	tick(reg, clk, {&yt2});
	r = rowOf(reg, clk.now,"aitum:YouTube Horizontal");
	CHECK(r->state == OutputState::Connecting);
	CHECK_FALSE(r->bytes.has());
	CHECK_FALSE(r->dropped.has());
	CHECK(reg.rowCount() == 3); // no duplicate row
}

TEST_CASE("Inferred 'Connecting' for an Aitum object expires if nothing ever happens")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o = aitumOutput(1, "Never Started");
	tick(reg, clk, {&o});
	CHECK(rowOf(reg, clk.now, "aitum:Never Started")->state == OutputState::Connecting);
	for (int i = 0; i < 29; ++i)
		tick(reg, clk, {&o});
	CHECK(rowOf(reg, clk.now, "aitum:Never Started")->state == OutputState::Connecting);
	tick(reg, clk, {&o}, {}, 2.0);
	auto r = rowOf(reg, clk.now, "aitum:Never Started");
	CHECK(r->state == OutputState::Idle);
	CHECK_FALSE(r->hasSession);

	// A real "starting" signal is never timed out by this rule.
	FakeOutput p = aitumOutput(2, "Slow Server");
	p.event(OutputEventType::Starting, clk.now);
	for (int i = 0; i < 40; ++i)
		tick(reg, clk, {&o, &p});
	CHECK(rowOf(reg, clk.now, "aitum:Slow Server")->state == OutputState::Connecting);
}

TEST_CASE("A 'stop' signal is not undone while obs_output_active is still true")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o = aitumOutput(1, "A");
	o.event(OutputEventType::Start, clk.now + 1);
	o.active = true;
	tick(reg, clk, {&o});
	CHECK(rowOf(reg, clk.now,"aitum:A")->state == OutputState::Live);

	// libobs emits "stop" before the deactivate thread clears 'active'.
	o.event(OutputEventType::Stop, clk.now + 1, StopCode::Success);
	tick(reg, clk, {&o});
	CHECK(rowOf(reg, clk.now,"aitum:A")->state == OutputState::Stopped);
	tick(reg, clk, {&o});
	CHECK(rowOf(reg, clk.now,"aitum:A")->state == OutputState::Stopped);
}

TEST_CASE("Reconnect: counters that reset on reconnect stay monotonic per session")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o;
	o.id = 5;
	o.kind = ProviderKind::Primary;
	o.name = "adv_stream";
	o.event(OutputEventType::Starting, clk.now);
	o.event(OutputEventType::Start, clk.now + 1);
	o.active = true;
	o.bytes = 1'000'000;
	o.total = 600;
	o.dropped = 4;
	tick(reg, clk, {&o});

	o.event(OutputEventType::Reconnect, clk.now + 1);
	o.reconnecting = true;
	tick(reg, clk, {&o});
	auto r = rowOf(reg, clk.now,"primary");
	CHECK(r->state == OutputState::Reconnecting);
	CHECK(r->health == Health::Bad);
	CHECK(r->reconnects == 1);

	// rtmp_output resets total_bytes_sent and dropped_frames in init_connect.
	o.event(OutputEventType::ReconnectSuccess, clk.now + 1);
	o.reconnecting = false;
	o.bytes = 100'000;
	o.dropped = 1;
	o.total = 660;
	tick(reg, clk, {&o});
	r = rowOf(reg, clk.now,"primary");
	CHECK(r->state == OutputState::Live);
	CHECK(r->reconnects == 1);
	CHECK(r->bytes.value() == 1'100'000);
	CHECK(r->dropped.value() == 5);
	REQUIRE(r->lastConnectedAgoNs.has());
}

TEST_CASE("Stop codes: disconnected and connect failure")
{
	SessionRegistry reg;
	Clock clk;

	FakeOutput a = aitumOutput(1, "A");
	a.event(OutputEventType::Start, clk.now);
	a.active = true;
	tick(reg, clk, {&a});
	a.event(OutputEventType::Stop, clk.now + 1, StopCode::Disconnected);
	a.active = false;
	tick(reg, clk, {}, {vanish(a)});
	auto r = rowOf(reg, clk.now,"aitum:A");
	CHECK(r->state == OutputState::Disconnected);
	CHECK(r->health == Health::Bad);
	CHECK(r->stopCode == StopCode::Disconnected);

	// Connection refused right away: object appears and fails within one tick.
	FakeOutput b = aitumOutput(2, "B");
	b.event(OutputEventType::Stop, clk.now + 1, StopCode::ConnectFailed, "Connection refused");
	tick(reg, clk, {&b});
	r = rowOf(reg, clk.now,"aitum:B");
	CHECK(r->state == OutputState::Error);
	CHECK(r->lastError == "Connection refused");

	auto summary = computeSummary(reg.snapshot(clk.now), SystemSnapshot{});
	CHECK(summary.failed == 2);
	CHECK(summary.overall == Health::Bad);
}

TEST_CASE("Unsupported metrics are N/A (SRT via ffmpeg_mpegts_muxer), measured ones are not")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput s = aitumOutput(3, "SRT dest");
	s.type = "ffmpeg_mpegts_muxer";
	s.event(OutputEventType::Start, clk.now);
	s.active = true;
	for (int i = 0; i < 3; ++i) {
		s.bytes += 500'000;
		s.total += 60;
		tick(reg, clk, {&s});
	}
	auto r = rowOf(reg, clk.now,"aitum:SRT dest");
	CHECK(r->kbps.has());
	CHECK(r->totalFrames.has());
	CHECK_FALSE(r->dropped.has());
	CHECK(r->dropped.reason() == Unavailable::NotSupported);
	CHECK_FALSE(r->dropPct.has());
	CHECK_FALSE(r->congestion.has());
	CHECK(r->congestion.reason() == Unavailable::NotSupported);
	CHECK_FALSE(r->connectTimeMs.has());
	CHECK(r->connectTimeMs.reason() == Unavailable::NotSupported);
}

TEST_CASE("Connect time: a 0 ms handshake (loopback) is real once connected, N/A before")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o = aitumOutput(1, "Loop");
	o.connectMs = 0; // rtmp_output: 0 before connecting
	tick(reg, clk, {&o});
	auto r = rowOf(reg, clk.now, "aitum:Loop");
	CHECK(r->state == OutputState::Connecting);
	CHECK_FALSE(r->connectTimeMs.has());

	o.event(OutputEventType::Start, clk.now);
	o.active = true; // connected in < 1 ms -> still 0
	tick(reg, clk, {&o});
	r = rowOf(reg, clk.now, "aitum:Loop");
	REQUIRE(r->connectTimeMs.has());
	CHECK(r->connectTimeMs.value() == 0);
}

TEST_CASE("A non-zero value proves support for an unknown output type")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput g;
	g.id = 9;
	g.name = "some_plugin_stream";
	g.type = "some_plugin_output";
	g.event(OutputEventType::Start, clk.now);
	g.active = true;
	g.total = 100;
	tick(reg, clk, {&g});
	auto r = rowOf(reg, clk.now,"output:some_plugin_stream|ingest.example.com");
	REQUIRE(r);
	CHECK_FALSE(r->dropped.has()); // 0 from an unknown type is not trusted
	g.dropped = 3;
	g.total = 200;
	tick(reg, clk, {&g});
	r = rowOf(reg, clk.now,"output:some_plugin_stream|ingest.example.com");
	REQUIRE(r->dropped.has());
	CHECK(r->dropped.value() == 3);
}

TEST_CASE("Identical names (obs-multi-rtmp) become separate rows and are both counted")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput a, b;
	a.id = 1;
	b.id = 2;
	a.kind = b.kind = ProviderKind::MultiRtmp;
	a.name = b.name = "multi-output";
	a.host = b.host = "live.twitch.tv";
	for (FakeOutput *f : {&a, &b}) {
		f->event(OutputEventType::Start, clk.now);
		f->active = true;
	}
	tick(reg, clk, {&a, &b});
	a.bytes = b.bytes = 750'000;
	tick(reg, clk, {&a, &b});
	auto rows = reg.snapshot(clk.now);
	REQUIRE(findRow(rows, "multirtmp:live.twitch.tv"));
	REQUIRE(findRow(rows, "multirtmp:live.twitch.tv#2"));
	CHECK(findRow(rows, "multirtmp:live.twitch.tv#2")->ordinal == 2);
	auto summary = computeSummary(rows, SystemSnapshot{});
	CHECK(summary.live == 2);
	CHECK(summary.sumKbps.value() == doctest::Approx(12000.0));
}

TEST_CASE("Output churn does not grow the row list without bound")
{
	SessionRegistry reg;
	Clock clk;
	for (uint64_t i = 0; i < 50; ++i) {
		FakeOutput g;
		g.id = 100 + i;
		g.name = "churn_" + std::to_string(i);
		g.event(OutputEventType::Start, clk.now);
		g.active = true;
		tick(reg, clk, {&g});
		g.event(OutputEventType::Stop, clk.now + 1);
		tick(reg, clk, {}, {vanish(g)});
	}
	CHECK(reg.rowCount() <= SessionRegistry::kMaxRetainedEndedRows + 1);
	reg.clearEnded();
	CHECK(reg.rowCount() == 1); // only the primary row remains
}

TEST_CASE("Primary output object replaced by OBS keeps a single primary row")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput first;
	first.id = 1;
	first.kind = ProviderKind::Primary;
	first.name = "adv_stream";
	tick(reg, clk, {&first});
	tick(reg, clk, {}, {vanish(first)});

	FakeOutput second = first;
	second.id = 2;
	second.event(OutputEventType::Starting, clk.now);
	second.event(OutputEventType::Start, clk.now + 1);
	second.active = true;
	tick(reg, clk, {&second});
	auto rows = reg.snapshot(clk.now);
	CHECK(rows.size() == 1);
	CHECK(rows[0].state == OutputState::Live);
}

TEST_CASE("Start time provenance: initial scan is unknown, polling-detected is measured")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput early;
	early.id = 1;
	early.name = "early";
	early.active = true;
	early.firstSeenActive = true;
	early.initialScan = true;
	early.bytes = 5'000'000;
	tick(reg, clk, {&early});
	auto r = rowOf(reg, clk.now,"output:early|ingest.example.com");
	CHECK(r->state == OutputState::Live);
	CHECK(r->startSource == StartSource::Unknown);
	CHECK_FALSE(r->durationNs.has());
	CHECK(r->durationNs.reason() == Unavailable::Unknown);
	CHECK_FALSE(r->avgKbps.has()); // bytes include pre-monitoring data

	FakeOutput later;
	later.id = 2;
	later.name = "later";
	later.active = true;
	later.firstSeenActive = true;
	tick(reg, clk, {&early, &later});
	r = rowOf(reg, clk.now,"output:later|ingest.example.com");
	CHECK(r->startSource == StartSource::Detected);
	CHECK(r->durationNs.has());
}

TEST_CASE("Polling fallback ends a live session without a stop signal after the grace period")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput g;
	g.id = 1;
	g.name = "quiet";
	g.active = true;
	tick(reg, clk, {&g});
	g.active = false;
	tick(reg, clk, {&g});
	CHECK(rowOf(reg, clk.now,"output:quiet|ingest.example.com")->state == OutputState::Live);
	tick(reg, clk, {&g});
	auto r = rowOf(reg, clk.now,"output:quiet|ingest.example.com");
	CHECK(r->state == OutputState::Stopped);
	CHECK(r->endReasonUnknown);

	// A late "stop" signal refines the reason.
	g.event(OutputEventType::Stop, clk.now, StopCode::Disconnected);
	tick(reg, clk, {&g});
	r = rowOf(reg, clk.now,"output:quiet|ingest.example.com");
	CHECK(r->state == OutputState::Disconnected);
	CHECK_FALSE(r->endReasonUnknown);
}

TEST_CASE("Renamed / deleted Aitum destinations disappear once not live; unknown config retains rows")
{
	SessionRegistry reg;
	Clock clk;
	reg.setConfiguredDestinations(ProviderKind::Aitum, {{ProviderKind::Aitum, "Old Name", false}});
	CHECK(rowOf(reg, clk.now,"aitum:Old Name"));

	FakeOutput live = aitumOutput(1, "Old Name");
	live.event(OutputEventType::Start, clk.now);
	live.active = true;
	tick(reg, clk, {&live});

	reg.setConfiguredDestinations(ProviderKind::Aitum, {{ProviderKind::Aitum, "New Name", true}});
	// still live -> kept
	CHECK(rowOf(reg, clk.now,"aitum:Old Name"));
	auto n = rowOf(reg, clk.now, "aitum:New Name");
	REQUIRE(n);
	CHECK(n->vertical);

	live.event(OutputEventType::Stop, clk.now);
	tick(reg, clk, {}, {vanish(live)});
	CHECK_FALSE(rowOf(reg, clk.now,"aitum:Old Name"));

	// Config becomes unknown (websocket unavailable): rows are retained.
	reg.forgetConfiguredDestinations(ProviderKind::Aitum);
	FakeOutput other = aitumOutput(2, "Other");
	other.event(OutputEventType::Start, clk.now);
	other.active = true;
	tick(reg, clk, {&other});
	other.event(OutputEventType::Stop, clk.now);
	tick(reg, clk, {}, {vanish(other)});
	CHECK(rowOf(reg, clk.now,"aitum:Other"));
}

TEST_CASE("Reset statistics restarts frame counters like the native dock")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o = aitumOutput(1, "A");
	o.event(OutputEventType::Start, clk.now);
	o.active = true;
	o.total = 1000;
	o.dropped = 50;
	tick(reg, clk, {&o});
	reg.resetStatistics();
	o.total = 1100;
	o.dropped = 51;
	tick(reg, clk, {&o});
	auto r = rowOf(reg, clk.now,"aitum:A");
	CHECK(r->countsSinceReset);
	CHECK(r->dropped.value() == 1);
	CHECK(r->totalFrames.value() == 100);
	CHECK(r->dropPct.value() == doctest::Approx(1.0));
}

TEST_CASE("Drop health: cumulative and recent drop percentages")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o = aitumOutput(1, "A");
	o.event(OutputEventType::Start, clk.now);
	o.active = true;
	tick(reg, clk, {&o});
	for (int i = 0; i < 5; ++i) {
		o.total += 60;
		o.dropped += 6; // 10 % dropping
		tick(reg, clk, {&o});
	}
	auto r = rowOf(reg, clk.now,"aitum:A");
	CHECK(r->dropHealth == Health::Bad);
	CHECK(r->health == Health::Bad);
	CHECK(r->state == OutputState::Live);
}
