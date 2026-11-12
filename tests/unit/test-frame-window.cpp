// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rendering/encoding-lag counting window (1.0.1). Every expectation mirrors
// OBS 33.0.1 frontend/widgets/OBSBasicStats.cpp (InitializeValues, Update,
// Reset), except that 0 counted frames give an N/A percentage, not "0.0%".
#include <doctest.h>

#include <core/format.hpp>
#include <core/frame-window.hpp>

#include <cstdint>
#include <limits>

using namespace msd;

namespace {
uint64_t v(const Metric<uint64_t> &m)
{
	REQUIRE(m.has());
	return m.value();
}
} // namespace

TEST_CASE("FrameWindow: startup baseline excludes frames lagged while OBS was loading")
{
	// 600 frames rendered and 10 lagged during startup; OBS finishes loading.
	FrameWindow w;
	w.baseline(600, 10);
	CHECK(w.hasBaseline());

	FrameCounts c = w.sample(600, 10);
	CHECK(v(c.total) == 0);
	CHECK(v(c.affected) == 0);
	CHECK_FALSE(c.percent.has()); // nothing counted yet: N/A, never a fake 0.0%
	CHECK(c.percent.reason() == Unavailable::NotMeasured);

	// 2307 more frames, no new lag: native shows "0 / 2307 (0.0%)".
	c = w.sample(600 + 2307, 10);
	CHECK(v(c.total) == 2307);
	CHECK(v(c.affected) == 0);
	REQUIRE(c.percent.has());
	CHECK(c.percent.value() == doctest::Approx(0.0));
	CHECK(fmt::frames(v(c.affected), v(c.total), c.percent.value()) == "0 / 2307 (0.0%)");
}

TEST_CASE("FrameWindow: percentage = affected * 100 / total, like the native dock")
{
	FrameWindow w;
	w.baseline(1000, 5);
	FrameCounts c = w.sample(1000 + 25142, 5 + 720);
	CHECK(v(c.total) == 25142);
	CHECK(v(c.affected) == 720);
	REQUIRE(c.percent.has());
	CHECK(c.percent.value() == doctest::Approx(720.0 * 100.0 / 25142.0));
	CHECK(fmt::frames(720, 25142, c.percent.value()) == "720 / 25142 (2.9%)");
	CHECK(fmt::framesCompact(720, c.percent.value()) == "720 (2.9%)");

	c = w.sample(1000 + 200, 5 + 200); // every frame lagged
	CHECK(c.percent.value() == doctest::Approx(100.0));
}

TEST_CASE("FrameWindow: without a baseline the first sample becomes it (native Reset semantics)")
{
	FrameWindow w;
	CHECK_FALSE(w.hasBaseline());
	bool rebased = false;
	FrameCounts c = w.sample(5000, 40, &rebased);
	CHECK(rebased);
	CHECK(v(c.total) == 0);
	CHECK(v(c.affected) == 0);
	c = w.sample(5060, 43, &rebased);
	CHECK_FALSE(rebased);
	CHECK(v(c.total) == 60);
	CHECK(v(c.affected) == 3);

	// Reset(): invalidate, then the next sample is the new start.
	w.invalidate();
	c = w.sample(9000, 50, &rebased);
	CHECK(rebased);
	CHECK(v(c.total) == 0);
	c = w.sample(9030, 50);
	CHECK(v(c.total) == 30);
	CHECK(v(c.affected) == 0);
}

TEST_CASE("FrameWindow: counters going backwards re-baseline instead of underflowing")
{
	// After a video reset the main video_t is new and its total/skipped
	// counters restart; 1.0.0 subtracted the old baseline and wrapped around.
	FrameWindow w;
	w.baseline(100000, 30);
	bool rebased = false;
	FrameCounts c = w.sample(12, 0, &rebased);
	CHECK(rebased);
	CHECK(v(c.total) == 0);
	CHECK(v(c.affected) == 0);
	c = w.sample(72, 1, &rebased);
	CHECK_FALSE(rebased);
	CHECK(v(c.total) == 60);
	CHECK(v(c.affected) == 1);

	// Only the affected counter going backwards also re-baselines.
	w.baseline(500, 9);
	c = w.sample(510, 3, &rebased);
	CHECK(rebased);
	CHECK(v(c.total) == 0);
}

TEST_CASE("FrameWindow: explicit re-baseline (video reset / Reset button) restarts counting")
{
	FrameWindow w;
	w.baseline(0, 0);
	FrameCounts c = w.sample(3000, 90);
	CHECK(v(c.affected) == 90);
	w.baseline(3000, 90); // video_reset at this point
	c = w.sample(3300, 90);
	CHECK(v(c.total) == 300);
	CHECK(v(c.affected) == 0);
	CHECK(c.percent.value() == doctest::Approx(0.0));
	CHECK(w.firstTotal() == 3000);
	CHECK(w.firstAffected() == 90);
}

TEST_CASE("FrameWindow: 32-bit counters near the wrap point are still differenced correctly")
{
	const uint32_t near = std::numeric_limits<uint32_t>::max() - 10;
	FrameWindow w;
	w.baseline(near, 7);
	FrameCounts c = w.sample(near + 10, 8);
	CHECK(v(c.total) == 10);
	CHECK(v(c.affected) == 1);
	CHECK(c.percent.value() == doctest::Approx(10.0));
}
