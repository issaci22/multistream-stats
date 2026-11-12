// SPDX-License-Identifier: GPL-2.0-or-later
#include "layout-math.hpp"

#include <algorithm>

namespace msd::layout {

bool threshold(bool current, int available, int required, int margin)
{
	return current ? available >= required : available >= required + std::max(0, margin);
}

bool isWide(int width, int height, bool current, int margin)
{
	if (width <= 0 || height <= 0)
		return current;
	return threshold(current, width, height + 1, margin);
}

int gridColumns(int width, int pairWidth, int gap, int items, int maxColumns)
{
	if (items <= 1 || maxColumns <= 1 || pairWidth <= 0)
		return 1;
	int cols = (width + gap) / (pairWidth + gap);
	cols = std::clamp(cols, 1, std::min(maxColumns, items));
	// Balance: 7 items in 3 columns would be 3+3+1; prefer as few columns as
	// give the same height (no near-empty last column).
	const int rows = rowsPerColumn(items, cols);
	return (items + rows - 1) / rows;
}

int rowsPerColumn(int items, int columns)
{
	if (items <= 0)
		return 0;
	columns = std::max(1, columns);
	return (items + columns - 1) / columns;
}

TablePlan planTable(const TableInput &in, const TablePlan &current, int margin)
{
	TablePlan plan;
	const int n = (int)in.cells.size();
	const int nameMin = std::min(in.nameMin, in.nameNatural);
	int sum = 0;
	int best = 0;
	int bestSum = 0;
	for (int k = 1; k <= n; ++k) {
		sum += in.cells[(size_t)k - 1];
		const int need = in.lead + nameMin + sum + in.spacing * k;
		// More columns than currently shown need the hysteresis margin;
		// keeping the current ones does not.
		const bool shownNow = current.table && k <= current.columns;
		if (!threshold(shownNow, in.width, need, margin))
			break;
		best = k;
		bestSum = sum;
	}
	if (best < in.requiredCells || best == 0)
		return plan; // stacked

	plan.table = true;
	plan.columns = best;
	const int rest = in.width - in.lead - bestSum - in.spacing * best;
	plan.nameWidth = std::clamp(rest, nameMin, std::max(nameMin, in.nameNatural));
	const int leftover = rest - plan.nameWidth;
	plan.pad = std::clamp(leftover / (best + 1), 0, std::max(0, in.maxPad));
	return plan;
}

} // namespace msd::layout
