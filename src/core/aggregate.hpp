// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "snapshot.hpp"

#include <vector>

namespace msd {

// Summary across all rows. Each output object belongs to exactly one row, so
// sums never double count (the primary output is one row like any other).
SummarySnapshot computeSummary(const std::vector<RowSnapshot> &rows, const SystemSnapshot &system);

// Worst of the system-wide indicators (FPS, render time, rendering lag,
// encoding lag).
Health systemHealth(const SystemSnapshot &system);

// Display rule (1.1.0). The registry keeps every row it knows (configured
// Aitum destinations, finished sessions) because it needs them for restarts,
// ordering and failure reasons; the dock's output list shows only:
//   * OBS's built-in stream (ProviderKind::Primary), always, in any state;
//   * any other output while its session runs (connecting, live,
//     reconnecting, stopping).
// Anything in a terminal state (stopped, disconnected, error), configured but
// not started, or never started is not listed.
bool isRowDisplayed(const RowSnapshot &row);
std::vector<RowSnapshot> displayedRows(std::vector<RowSnapshot> rows);

// The most recent failure of a non-listed output: a non-primary row whose
// session ended Disconnected / Error and that has not been restarted or
// cleared since. Shown as one summary line so the reason is not lost when
// the output leaves the list. nullptr if there is none.
const RowSnapshot *lastFailure(const std::vector<RowSnapshot> &all);

} // namespace msd
