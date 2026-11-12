// SPDX-License-Identifier: GPL-2.0-or-later
#include <doctest.h>

#include <core/layout-math.hpp>

using namespace msd::layout;

TEST_CASE("threshold: entering needs the margin, staying does not")
{
	CHECK_FALSE(threshold(false, 100, 100, 17));
	CHECK(threshold(false, 117, 100, 17));
	CHECK(threshold(true, 100, 100, 17));
	CHECK_FALSE(threshold(true, 99, 100, 17));
}

TEST_CASE("isWide: by aspect, with hysteresis")
{
	CHECK_FALSE(isWide(280, 900, false, 17));
	CHECK_FALSE(isWide(480, 700, false, 17));
	CHECK(isWide(1000, 260, false, 17));
	CHECK(isWide(700, 400, false, 17));
	// Square-ish: no flip-flop within one scroll-bar width.
	CHECK_FALSE(isWide(510, 500, false, 17));
	CHECK(isWide(520, 500, false, 17));
	CHECK(isWide(503, 500, true, 17));
	CHECK_FALSE(isWide(499, 500, true, 17));
	// Unknown size keeps the current decision.
	CHECK(isWide(0, 0, true, 17));
}

TEST_CASE("gridColumns: fits, caps, balances")
{
	CHECK(gridColumns(300, 250, 24, 7, 2) == 1);
	CHECK(gridColumns(524, 250, 24, 7, 2) == 2);
	CHECK(gridColumns(523, 250, 24, 7, 2) == 1);
	CHECK(gridColumns(2000, 250, 24, 7, 2) == 2);   // maxColumns
	CHECK(gridColumns(2000, 250, 24, 2, 4) == 2);   // never more than items
	CHECK(gridColumns(2000, 250, 24, 1, 4) == 1);
	CHECK(gridColumns(2000, 100, 10, 5, 4) == 3);   // 4 columns would be 2+2+1+0 -> 3 (2+2+1)
	CHECK(gridColumns(2000, 100, 10, 7, 3) == 3);   // 3+3+1
	CHECK(rowsPerColumn(7, 2) == 4);
	CHECK(rowsPerColumn(7, 3) == 3);
	CHECK(rowsPerColumn(0, 3) == 0);
}

namespace {
TableInput sample(int width)
{
	TableInput in;
	in.width = width;
	in.lead = 40;
	in.spacing = 6;
	in.nameNatural = 160;
	in.nameMin = 80;
	in.cells = {90, 90, 120, 70, 70}; // status, bitrate, dropped, data, duration
	in.requiredCells = 4;
	in.maxPad = 30;
	return in;
}
} // namespace

TEST_CASE("planTable: stacked below the required columns, table above")
{
	// lead 40 + name 80 + 370 + 4*6 = 514 for four columns.
	CHECK_FALSE(planTable(sample(513), {}, 17).table);
	CHECK_FALSE(planTable(sample(520), {}, 17).table); // entering needs the margin
	const TablePlan p = planTable(sample(531), {}, 17);
	CHECK(p.table);
	CHECK(p.columns == 4);
	CHECK(p.nameWidth == 531 - 40 - 370 - 24);
	CHECK(p.pad == 0);
}

TEST_CASE("planTable: hysteresis keeps the table while it still fits")
{
	TablePlan cur = planTable(sample(531), {}, 17);
	REQUIRE(cur.table);
	CHECK(planTable(sample(514), cur, 17).table);
	CHECK_FALSE(planTable(sample(513), cur, 17).table);
}

TEST_CASE("planTable: optional duration column, natural name width, padding")
{
	// Duration: 514 + 70 + 6 = 590, entering needs +17.
	CHECK(planTable(sample(600), {}, 17).columns == 4);
	TablePlan five = planTable(sample(607), {}, 17);
	CHECK(five.columns == 5);
	CHECK(planTable(sample(590), five, 17).columns == 5);
	CHECK(planTable(sample(589), five, 17).columns == 4);

	// Lots of room: name at its natural width, the rest padded (capped).
	const TablePlan wide = planTable(sample(1400), {}, 17);
	CHECK(wide.columns == 5);
	CHECK(wide.nameWidth == 160);
	CHECK(wide.pad == 30);

	// Some room: name grows first, then padding.
	const TablePlan mid = planTable(sample(700), {}, 17);
	CHECK(mid.nameWidth == 160);
	CHECK(mid.pad == (700 - 40 - 440 - 30 - 160) / 6);
}

TEST_CASE("planTable: a short natural name is never padded up to nameMin")
{
	TableInput in = sample(531);
	in.nameNatural = 50;
	const TablePlan p = planTable(in, {}, 17);
	CHECK(p.table);
	CHECK(p.nameWidth == 50);
}
