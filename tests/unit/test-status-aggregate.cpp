// SPDX-License-Identifier: GPL-2.0-or-later
#include <doctest.h>

#include <core/aggregate.hpp>
#include <core/status-model.hpp>

using namespace msd;

TEST_CASE("Stop codes map to end states")
{
	CHECK(stateForStopCode(StopCode::Success) == OutputState::Stopped);
	CHECK(stateForStopCode(StopCode::Disconnected) == OutputState::Disconnected);
	CHECK(stateForStopCode(StopCode::ConnectFailed) == OutputState::Error);
	CHECK(stateForStopCode(StopCode::InvalidStream) == OutputState::Error);
	CHECK(stateForStopCode(StopCode::EncodeError) == OutputState::Error);
}

TEST_CASE("Native thresholds: > 1% warning, > 5% danger")
{
	CHECK(percentHealth(Metric<double>::of(0.0)) == Health::Good);
	CHECK(percentHealth(Metric<double>::of(1.0)) == Health::Good);
	CHECK(percentHealth(Metric<double>::of(1.01)) == Health::Warning);
	CHECK(percentHealth(Metric<double>::of(5.0)) == Health::Warning);
	CHECK(percentHealth(Metric<double>::of(5.01)) == Health::Bad);
	CHECK(percentHealth(Metric<double>::na(Unavailable::NotSupported)) == Health::Inactive);
}

TEST_CASE("FPS / render-time / congestion thresholds")
{
	CHECK(fpsHealth(Metric<double>::of(60.0), 60.0) == Health::Good);
	CHECK(fpsHealth(Metric<double>::of(56.0), 60.0) == Health::Warning); // < 57
	CHECK(fpsHealth(Metric<double>::of(47.0), 60.0) == Health::Bad);     // < 48
	CHECK(renderTimeHealth(Metric<double>::of(5.0), 60.0) == Health::Good);
	CHECK(renderTimeHealth(Metric<double>::of(13.0), 60.0) == Health::Warning); // > 12.5
	CHECK(renderTimeHealth(Metric<double>::of(17.0), 60.0) == Health::Bad);     // > 16.67
	CHECK(congestionHealth(Metric<double>::of(0.2)) == Health::Good);
	CHECK(congestionHealth(Metric<double>::of(0.5)) == Health::Warning);
	CHECK(congestionHealth(Metric<double>::of(0.9)) == Health::Bad);
}

namespace {
RowSnapshot liveRow(double kbps, uint64_t dropped, uint64_t total)
{
	RowSnapshot r;
	r.state = OutputState::Live;
	r.health = Health::Good;
	r.kbps = Metric<double>::of(kbps);
	r.dropped = Metric<uint64_t>::of(dropped);
	r.totalFrames = Metric<uint64_t>::of(total);
	return r;
}
} // namespace

TEST_CASE("Summary sums measured bitrates and drops without double counting")
{
	std::vector<RowSnapshot> rows;
	rows.push_back(liveRow(6000, 0, 1000));
	rows.push_back(liveRow(10000, 30, 1000));
	RowSnapshot srt = liveRow(4000, 0, 500);
	srt.dropped = Metric<uint64_t>::na(Unavailable::NotSupported); // SRT output
	rows.push_back(srt);
	RowSnapshot stopped;
	stopped.state = OutputState::Stopped;
	rows.push_back(stopped);

	SystemSnapshot sys;
	auto s = computeSummary(rows, sys);
	CHECK(s.rows == 4);
	CHECK(s.running == 3);
	CHECK(s.live == 3);
	REQUIRE(s.sumKbps.has());
	CHECK(s.sumKbps.value() == doctest::Approx(20000.0));
	REQUIRE(s.droppedSum.has());
	CHECK(s.droppedSum.value() == 30);
	CHECK(s.framesSum.value() == 2000); // SRT frames excluded: its drops are unknown
	CHECK(s.dropPct.value() == doctest::Approx(1.5));
	CHECK(s.dropUnsupported == 1);
	CHECK(s.overall == Health::Good);
}

TEST_CASE("Summary never reports 0 when nothing could be measured")
{
	std::vector<RowSnapshot> rows;
	RowSnapshot r;
	r.state = OutputState::Live;
	r.health = Health::Good;
	r.kbps = Metric<double>::na(Unavailable::NotMeasured);
	r.dropped = Metric<uint64_t>::na(Unavailable::NotSupported);
	rows.push_back(r);
	auto s = computeSummary(rows, SystemSnapshot{});
	CHECK_FALSE(s.sumKbps.has());
	CHECK(s.sumKbps.reason() == Unavailable::NotMeasured);
	CHECK(s.unmeasured == 1);
	CHECK_FALSE(s.droppedSum.has());
	CHECK(s.droppedSum.reason() == Unavailable::NotSupported);

	auto empty = computeSummary({}, SystemSnapshot{});
	CHECK_FALSE(empty.sumKbps.has());
	CHECK(empty.sumKbps.reason() == Unavailable::NotActive);
	CHECK(empty.overall == Health::Inactive);
}

TEST_CASE("Overall health: worst row; system health only matters while streaming")
{
	SystemSnapshot sys;
	sys.renderLagHealth = Health::Bad;

	auto idle = computeSummary({}, sys);
	CHECK(idle.overall == Health::Inactive);
	CHECK(idle.systemHealth == Health::Bad);

	std::vector<RowSnapshot> rows{liveRow(6000, 0, 100)};
	auto live = computeSummary(rows, sys);
	CHECK(live.overall == Health::Bad);

	RowSnapshot rec;
	rec.state = OutputState::Reconnecting;
	rec.health = Health::Bad;
	rec.kbps = Metric<double>::of(0.0);
	rows.push_back(rec);
	auto s = computeSummary(rows, SystemSnapshot{});
	CHECK(s.reconnecting == 1);
	CHECK(s.problems == 1);
	CHECK(s.overall == Health::Bad);
}
