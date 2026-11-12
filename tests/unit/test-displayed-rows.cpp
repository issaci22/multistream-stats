// SPDX-License-Identifier: GPL-2.0-or-later
//
// 1.1.0 display rule: the OBS built-in stream is always shown; every other
// output only while it runs (connecting, live, reconnecting, stopping).
// The most recent failure is kept separately (lastFailure).
// Driven through the real SessionRegistry with synthetic observations, exactly
// like the collector does each tick.
#include <doctest.h>

#include "test-helpers.hpp"

#include <core/aggregate.hpp>

#include <algorithm>
#include <set>

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

std::vector<RowSnapshot> shown(const SessionRegistry &reg, const Clock &clk)
{
	return displayedRows(reg.snapshot(clk.now));
}

std::set<std::string> shownKeys(const SessionRegistry &reg, const Clock &clk)
{
	std::set<std::string> keys;
	for (const RowSnapshot &r : shown(reg, clk))
		keys.insert(r.key);
	return keys;
}

using Keys = std::set<std::string>;

// Aitum creates the object on start (inactive while connecting), then "start".
void startLive(FakeOutput &f, Clock &clk)
{
	f.event(OutputEventType::Starting, clk.now);
	f.event(OutputEventType::Activate, clk.now + 1);
	f.event(OutputEventType::Start, clk.now + 2);
	f.active = true;
}

// User stops the destination: "stopping", "stop" (success), Aitum releases it.
VanishedOutput userStop(FakeOutput &f, Clock &clk)
{
	f.event(OutputEventType::Stopping, clk.now + 1);
	f.event(OutputEventType::Stop, clk.now + 2, StopCode::Success);
	f.active = false;
	return vanish(f);
}

const std::vector<ConfiguredDestination> kConfig = {{ProviderKind::Aitum, "YouTube Horizontal", false},
						    {ProviderKind::Aitum, "Kick Output", false},
						    {ProviderKind::Aitum, "Some Other Platform", true}};

} // namespace

TEST_CASE("Idle: only the OBS built-in stream is shown, configured Aitum destinations are hidden")
{
	SessionRegistry reg;
	Clock clk;
	reg.setConfiguredDestinations(ProviderKind::Aitum, kConfig);
	CHECK(reg.rowCount() == 4); // the registry still knows every destination

	const auto rows = shown(reg, clk);
	REQUIRE(rows.size() == 1);
	CHECK(rows[0].key == "primary");
	CHECK(rows[0].kind == ProviderKind::Primary);
	CHECK(rows[0].state == OutputState::Idle);

	const auto s = computeSummary(rows, SystemSnapshot{});
	CHECK(s.rows == 1);
	CHECK(s.running == 0);
	CHECK(s.live == 0);
	CHECK(s.overall == Health::Inactive); // "No active streams"
}

TEST_CASE("Scenarios 1-4: Aitum outputs appear and disappear with their streams; primary stays")
{
	SessionRegistry reg;
	Clock clk;
	reg.setConfiguredDestinations(ProviderKind::Aitum, kConfig);

	// Scenario 1: start YouTube through Aitum (OBS's own stream not used).
	FakeOutput yt = aitumOutput(10, "YouTube Horizontal");
	tick(reg, clk, {&yt}); // object exists, still connecting
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:YouTube Horizontal"});
	CHECK(rowOf(reg, clk.now, "aitum:YouTube Horizontal")->state == OutputState::Connecting);
	startLive(yt, clk);
	tick(reg, clk, {&yt});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:YouTube Horizontal"});
	CHECK(rowOf(reg, clk.now, "aitum:YouTube Horizontal")->state == OutputState::Live);
	CHECK(rowOf(reg, clk.now, "primary")->state == OutputState::Idle);

	// Scenario 2: Kick starts while YouTube runs; both shown, independent.
	FakeOutput kick = aitumOutput(11, "Kick Output");
	startLive(kick, clk);
	tick(reg, clk, {&yt, &kick});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:YouTube Horizontal", "aitum:Kick Output"});
	auto s = computeSummary(shown(reg, clk), SystemSnapshot{});
	CHECK(s.rows == 3);
	CHECK(s.live == 2);

	// Scenario 3: Kick stops; only Kick disappears.
	tick(reg, clk, {&yt}, {userStop(kick, clk)});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:YouTube Horizontal"});
	CHECK(rowOf(reg, clk.now, "aitum:YouTube Horizontal")->state == OutputState::Live);
	CHECK(rowOf(reg, clk.now, "aitum:Kick Output")->state == OutputState::Stopped); // known, hidden

	// Scenario 4: all Aitum streams stopped.
	tick(reg, clk, {}, {userStop(yt, clk)});
	const auto rows = shown(reg, clk);
	REQUIRE(rows.size() == 1);
	CHECK(rows[0].key == "primary");
	CHECK(rows[0].state == OutputState::Idle); // "Not streaming"
	s = computeSummary(rows, SystemSnapshot{});
	CHECK(s.running == 0);
	CHECK(s.overall == Health::Inactive);

	// Restarting a destination brings it back with a fresh session, no duplicate.
	FakeOutput yt2 = aitumOutput(12, "YouTube Horizontal");
	startLive(yt2, clk);
	tick(reg, clk, {&yt2});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:YouTube Horizontal"});
	CHECK(reg.rowCount() == 4);
}

TEST_CASE("The built-in stream is shown in every state")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput p;
	p.id = 1;
	p.kind = ProviderKind::Primary;
	p.name = "adv_stream";
	tick(reg, clk, {&p}); // output object exists, not streaming
	CHECK(shownKeys(reg, clk) == Keys{"primary"});

	p.event(OutputEventType::Starting, clk.now);
	tick(reg, clk, {&p});
	CHECK(rowOf(reg, clk.now, "primary")->state == OutputState::Connecting);
	CHECK(shownKeys(reg, clk) == Keys{"primary"});

	p.event(OutputEventType::Start, clk.now);
	p.active = true;
	tick(reg, clk, {&p});
	CHECK(rowOf(reg, clk.now, "primary")->state == OutputState::Live);

	p.event(OutputEventType::Stop, clk.now, StopCode::Success);
	p.active = false;
	tick(reg, clk, {&p});
	CHECK(rowOf(reg, clk.now, "primary")->state == OutputState::Stopped);
	CHECK(shownKeys(reg, clk) == Keys{"primary"});

	reg.clearEnded();
	CHECK(shownKeys(reg, clk) == Keys{"primary"});
}

TEST_CASE("Transitional states stay visible: connecting, reconnecting, stopping")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput a = aitumOutput(1, "Dest A");
	a.event(OutputEventType::Starting, clk.now);
	tick(reg, clk, {&a});
	CHECK(rowOf(reg, clk.now, "aitum:Dest A")->state == OutputState::Connecting);
	CHECK(shownKeys(reg, clk).count("aitum:Dest A") == 1);

	a.event(OutputEventType::Start, clk.now);
	a.active = true;
	tick(reg, clk, {&a});
	a.event(OutputEventType::Reconnect, clk.now);
	a.reconnecting = true;
	tick(reg, clk, {&a});
	CHECK(rowOf(reg, clk.now, "aitum:Dest A")->state == OutputState::Reconnecting);
	CHECK(shownKeys(reg, clk).count("aitum:Dest A") == 1);

	a.event(OutputEventType::ReconnectSuccess, clk.now);
	a.reconnecting = false;
	tick(reg, clk, {&a});
	a.event(OutputEventType::Stopping, clk.now);
	tick(reg, clk, {&a});
	CHECK(rowOf(reg, clk.now, "aitum:Dest A")->state == OutputState::Stopping);
	CHECK(shownKeys(reg, clk).count("aitum:Dest A") == 1);

	a.event(OutputEventType::Stop, clk.now, StopCode::Success);
	a.active = false;
	tick(reg, clk, {}, {vanish(a)});
	CHECK(shownKeys(reg, clk) == Keys{"primary"});
}

TEST_CASE("Inferred 'Connecting' is shown, and hidden again once it expires")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput o = aitumOutput(1, "Never Connects");
	tick(reg, clk, {&o});
	CHECK(shownKeys(reg, clk).count("aitum:Never Connects") == 1);
	for (int i = 0; i < 31; ++i)
		tick(reg, clk, {&o});
	CHECK(rowOf(reg, clk.now, "aitum:Never Connects")->state == OutputState::Idle);
	CHECK(shownKeys(reg, clk) == Keys{"primary"});
}

TEST_CASE("Failed outputs leave the list at once; the most recent failure is kept as information")
{
	SessionRegistry reg;
	Clock clk;
	reg.setConfiguredDestinations(ProviderKind::Aitum, kConfig);
	CHECK(lastFailure(reg.snapshot(clk.now)) == nullptr);

	// Kick streams alongside; it must be unaffected by YouTube's failure.
	FakeOutput other = aitumOutput(9, "Some Other Platform");
	startLive(other, clk);
	// Connection lost, OBS gave up reconnecting.
	FakeOutput yt = aitumOutput(1, "YouTube Horizontal");
	startLive(yt, clk);
	tick(reg, clk, {&yt, &other});
	yt.event(OutputEventType::Reconnect, clk.now);
	yt.reconnecting = true;
	tick(reg, clk, {&yt, &other});
	CHECK(shownKeys(reg, clk).count("aitum:YouTube Horizontal") == 1); // still trying: listed
	yt.event(OutputEventType::Stop, clk.now, StopCode::Disconnected);
	yt.active = false;
	tick(reg, clk, {&other}, {vanish(yt)});

	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:Some Other Platform"});
	auto all = reg.snapshot(clk.now);
	const RowSnapshot *f = lastFailure(all);
	REQUIRE(f);
	CHECK(f->key == "aitum:YouTube Horizontal");
	CHECK(f->state == OutputState::Disconnected);
	CHECK(f->stopCode == StopCode::Disconnected);
	CHECK(f->endNs > 0);
	auto s = computeSummary(shown(reg, clk), SystemSnapshot{});
	CHECK(s.failed == 0); // not an active output
	CHECK(s.live == 1);

	// Could not connect at all: never listed as active; becomes the latest failure.
	FakeOutput kick = aitumOutput(2, "Kick Output");
	kick.event(OutputEventType::Stop, clk.now, StopCode::ConnectFailed, "Connection refused");
	tick(reg, clk, {&kick, &other});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:Some Other Platform"});
	tick(reg, clk, {&other}, {vanish(kick)});
	all = reg.snapshot(clk.now);
	f = lastFailure(all);
	REQUIRE(f);
	CHECK(f->key == "aitum:Kick Output");
	CHECK(f->state == OutputState::Error);
	CHECK(f->lastError == "Connection refused");

	// Restarting Kick removes its failure; YouTube's older one is next.
	FakeOutput kick2 = aitumOutput(3, "Kick Output");
	startLive(kick2, clk);
	tick(reg, clk, {&kick2, &other});
	all = reg.snapshot(clk.now);
	f = lastFailure(all);
	REQUIRE(f);
	CHECK(f->key == "aitum:YouTube Horizontal");

	// "Clear Last Failure" (clearEnded) forgets the remaining failure.
	reg.clearEnded();
	CHECK(lastFailure(reg.snapshot(clk.now)) == nullptr);
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:Kick Output", "aitum:Some Other Platform"});
	CHECK(reg.rowCount() == 4); // configured destinations remain known
}

TEST_CASE("A failure of OBS's own stream stays in its (always listed) row, not in last failure")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput p;
	p.id = 1;
	p.kind = ProviderKind::Primary;
	p.name = "adv_stream";
	p.event(OutputEventType::Start, clk.now);
	p.active = true;
	tick(reg, clk, {&p});
	p.event(OutputEventType::Stop, clk.now, StopCode::Disconnected);
	p.active = false;
	tick(reg, clk, {&p});
	const auto rows = shown(reg, clk);
	REQUIRE(rows.size() == 1);
	CHECK(rows[0].state == OutputState::Disconnected);
	CHECK(lastFailure(reg.snapshot(clk.now)) == nullptr);
}

TEST_CASE("Configured entry and live object of one destination are one row; others are independent")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput a = aitumOutput(1, "Dest A");
	FakeOutput b = aitumOutput(2, "Dest B");
	startLive(a, clk);
	startLive(b, clk);
	tick(reg, clk, {&a, &b});
	// Aitum's configuration arrives after the outputs started (overlap).
	reg.setConfiguredDestinations(ProviderKind::Aitum,
				      {{ProviderKind::Aitum, "Dest A", false}, {ProviderKind::Aitum, "Dest B", false}});
	tick(reg, clk, {&a, &b});
	auto rows = shown(reg, clk);
	CHECK(rows.size() == 3);
	CHECK(std::count_if(rows.begin(), rows.end(), [](const RowSnapshot &r) { return r.key == "aitum:Dest A"; }) == 1);

	// B's object disappears without any stop signal (polling fallback):
	// only B ends, A keeps streaming.
	tick(reg, clk, {&a}, {vanish(b)});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:Dest A"});
	CHECK(rowOf(reg, clk.now, "aitum:Dest A")->state == OutputState::Live);
}

TEST_CASE("Destination names are data: any name works, none is special")
{
	SessionRegistry reg;
	Clock clk;
	const std::string unicode = "\xC3\x9Cnicode \xE9\x85\x8D\xE4\xBF\xA1"; // UTF-8, any name is data
	const std::vector<std::string> names = {"My Custom RTMP", "Twitch", "srt dest", unicode};
	std::vector<FakeOutput> outs;
	uint64_t id = 1;
	for (const std::string &n : names)
		outs.push_back(aitumOutput(id++, n));
	std::vector<FakeOutput *> live;
	for (FakeOutput &f : outs) {
		startLive(f, clk);
		live.push_back(&f);
	}
	tick(reg, clk, live);
	CHECK(shown(reg, clk).size() == names.size() + 1);
	tick(reg, clk, {&outs[0], &outs[1], &outs[3]}, {userStop(outs[2], clk)});
	CHECK(shownKeys(reg, clk) == Keys{"primary", "aitum:My Custom RTMP", "aitum:Twitch", "aitum:" + unicode});
}

TEST_CASE("Non-Aitum outputs follow the same rule (only the built-in stream is exempt)")
{
	SessionRegistry reg;
	Clock clk;
	FakeOutput g;
	g.id = 5;
	g.name = "some_plugin_stream";
	tick(reg, clk, {&g}); // exists, never started
	CHECK(shownKeys(reg, clk) == Keys{"primary"});
	g.event(OutputEventType::Start, clk.now);
	g.active = true;
	tick(reg, clk, {&g});
	CHECK(shownKeys(reg, clk).count("output:some_plugin_stream|ingest.example.com") == 1);
	g.event(OutputEventType::Stop, clk.now, StopCode::Success);
	g.active = false;
	tick(reg, clk, {&g});
	CHECK(shownKeys(reg, clk) == Keys{"primary"});

	FakeOutput m;
	m.id = 6;
	m.kind = ProviderKind::MultiRtmp;
	m.name = "multi-output";
	m.host = "live.example.net";
	m.event(OutputEventType::Start, clk.now);
	m.active = true;
	tick(reg, clk, {&m});
	CHECK(shownKeys(reg, clk).count("multirtmp:live.example.net") == 1);
}

TEST_CASE("isRowDisplayed: state table")
{
	RowSnapshot r;
	r.kind = ProviderKind::Aitum;
	CHECK_FALSE(isRowDisplayed(r)); // never started
	r.hasSession = true;
	for (OutputState st : {OutputState::Connecting, OutputState::Live, OutputState::Reconnecting, OutputState::Stopping}) {
		r.state = st;
		CHECK(isRowDisplayed(r));
	}
	for (OutputState st : {OutputState::Idle, OutputState::Stopped, OutputState::Disconnected, OutputState::Error}) {
		r.state = st;
		CHECK_FALSE(isRowDisplayed(r));
	}
	r.kind = ProviderKind::Primary;
	r.hasSession = false;
	r.state = OutputState::Idle;
	CHECK(isRowDisplayed(r));
	for (OutputState st : {OutputState::Stopped, OutputState::Disconnected, OutputState::Error, OutputState::Live}) {
		r.hasSession = true;
		r.state = st;
		CHECK(isRowDisplayed(r));
	}
}
