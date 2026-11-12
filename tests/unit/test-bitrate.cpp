// SPDX-License-Identifier: GPL-2.0-or-later
#include <doctest.h>

#include <core/bitrate-meter.hpp>

using namespace msd;

TEST_CASE("CounterAccumulator folds resets into a monotonic session total")
{
	CounterAccumulator c;
	CHECK_FALSE(c.hasValue());
	CHECK(c.update(100) == 100);
	CHECK(c.update(250) == 250);
	// rtmp_output resets total_bytes_sent on reconnect
	CHECK(c.update(10) == 260);
	CHECK(c.update(40) == 290);
	CHECK(c.total() == 290);
	c.reset();
	CHECK_FALSE(c.hasValue());
	CHECK(c.update(5) == 5);
}

TEST_CASE("BitrateMeter uses the native formula and needs two samples")
{
	BitrateMeter m;
	CHECK_FALSE(m.currentKbps().has());
	CHECK(m.currentKbps().reason() == Unavailable::NotMeasured);

	const uint64_t s = 1'000'000'000ULL;
	m.addSample(10 * s, 0);
	CHECK_FALSE(m.currentKbps().has());

	// 750,000 bytes in 1 s = 6,000,000 bits/s = 6000 kb/s
	m.addSample(11 * s, 750'000);
	REQUIRE(m.currentKbps().has());
	CHECK(m.currentKbps().value() == doctest::Approx(6000.0));

	// window (2 s) smooths: 12 s -> bytes 1,750,000 over 2 s = 7000 kb/s
	m.addSample(12 * s, 1'750'000);
	CHECK(m.currentKbps().value() == doctest::Approx(7000.0));

	// next sample drops the oldest point: (2,000,000-750,000)*8/2s = 5000 kb/s
	m.addSample(13 * s, 2'000'000);
	CHECK(m.currentKbps().value() == doctest::Approx(5000.0));
}

TEST_CASE("BitrateMeter: zero bytes is a real 0 kb/s, not N/A")
{
	BitrateMeter m;
	const uint64_t s = 1'000'000'000ULL;
	m.addSample(1 * s, 500);
	m.addSample(2 * s, 500);
	REQUIRE(m.currentKbps().has());
	CHECK(m.currentKbps().value() == doctest::Approx(0.0));
}

TEST_CASE("BitrateMeter ignores non-advancing clock and restarts on non-monotonic bytes")
{
	BitrateMeter m;
	const uint64_t s = 1'000'000'000ULL;
	m.addSample(5 * s, 1000);
	m.addSample(5 * s, 9999); // same timestamp: ignored
	CHECK_FALSE(m.currentKbps().has());
	m.addSample(5 * s + 5'000'000, 2000); // 5 ms later: below 10 ms guard
	CHECK_FALSE(m.currentKbps().has());
	m.addSample(7 * s, 100); // bytes went backwards -> restart
	CHECK_FALSE(m.currentKbps().has());
}

TEST_CASE("averageKbps")
{
	CHECK_FALSE(averageKbps(1000, 500'000'000ULL).has()); // < 1 s
	auto a = averageKbps(1'250'000, 2'000'000'000ULL);   // 10 Mbit / 2 s
	REQUIRE(a.has());
	CHECK(a.value() == doctest::Approx(5000.0));
}

TEST_CASE("CongestionSmoother mirrors the OBS status bar")
{
	CongestionSmoother c;
	CHECK_FALSE(c.value().has());
	c.addSample(0.0f);
	REQUIRE(c.value().has());
	CHECK(c.value().value() == doctest::Approx(0.0));
	c.addSample(1.0f); // avg(1,0)=0.5 < 1 -> 1.0
	c.addSample(0.0f); // avg(0,1)=0.5
	c.addSample(0.0f); // 0
	// window: 0, 1.0, 0.5, 0 -> mean 0.375
	CHECK(c.value().value() == doctest::Approx(0.375));
	c.addSample(2.0f); // clamped to 1
	CHECK(c.value().value() <= 1.0);
}

TEST_CASE("RecentDropWindow reports drops within the window only")
{
	RecentDropWindow w(10'000'000'000ULL);
	const uint64_t s = 1'000'000'000ULL;
	w.addSample(1 * s, 0, 0);
	CHECK_FALSE(w.recentPercent().has());
	w.addSample(2 * s, 0, 60);
	REQUIRE(w.recentPercent().has());
	CHECK(w.recentPercent().value() == doctest::Approx(0.0));
	w.addSample(3 * s, 6, 120); // 6 of 120 = 5%
	CHECK(w.recentPercent().value() == doctest::Approx(5.0));
	CHECK(w.recentDropped() == 6);
	// 12 seconds later without new drops -> window slides past the drops
	for (int i = 4; i <= 15; ++i)
		w.addSample((uint64_t)i * s, 6, (uint64_t)(60 * (i - 1)));
	CHECK(w.recentDropped() == 0);
}
