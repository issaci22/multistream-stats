// SPDX-License-Identifier: GPL-2.0-or-later
//
// Responsive-layout decisions of the dock (1.1.0), free of Qt so they can be
// unit-tested. All inputs are measured pixel widths (text widths from the
// current font), so the decisions hold at any DPI or font scale.
//
// Hysteresis: a threshold that switches the layout must not flip back when the
// switch itself changes the available width by a scroll bar (content becomes
// shorter -> vertical scroll bar disappears -> viewport grows, or the
// reverse). Switching *into* the roomier layout therefore needs `margin`
// extra pixels; staying in it only needs the plain requirement.
#pragma once

#include <vector>

namespace msd::layout {

// true if `available` satisfies `required`, with hysteresis around `current`.
bool threshold(bool current, int available, int required, int margin);

// Dock shape. Wide (width > height) docks use horizontal space like OBS's
// native Stats dock; tall docks stack vertically.
bool isWide(int width, int height, bool current, int margin);

// Label/value pairs per row of a StatGrid: as many columns as fit (each at
// least `pairWidth` wide, `gap` between columns), at most `maxColumns`, never
// more than there are items, and without an empty last column.
int gridColumns(int width, int pairWidth, int gap, int items, int maxColumns);
// Rows in the tallest column when `items` are filled column by column.
int rowsPerColumn(int items, int columns);

// Output table: [lead][name][cell 0]...[cell k-1] with `spacing` before each
// cell. `cells` are the natural widths of the possible columns in priority
// order; the first `requiredCells` must fit for a table at all.
struct TableInput {
	int width = 0;        // available width
	int lead = 0;         // disclosure arrow + status square incl. spacing
	int spacing = 0;      // between header items
	int nameNatural = 0;  // widest destination name (+ provider word)
	int nameMin = 0;      // names may elide down to this
	std::vector<int> cells;
	int requiredCells = 0;
	int maxPad = 0;       // extra width per column when there is room
};

struct TablePlan {
	bool table = false;
	int columns = 0;   // cells shown (requiredCells..cells.size())
	int nameWidth = 0; // name column width
	int pad = 0;       // added to every shown cell
	bool operator==(const TablePlan &o) const
	{
		return table == o.table && columns == o.columns && nameWidth == o.nameWidth && pad == o.pad;
	}
	bool operator!=(const TablePlan &o) const { return !(*this == o); }
};

TablePlan planTable(const TableInput &in, const TablePlan &current, int margin);

} // namespace msd::layout
