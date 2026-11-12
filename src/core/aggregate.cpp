// SPDX-License-Identifier: GPL-2.0-or-later
#include "aggregate.hpp"

#include <algorithm>

namespace msd {

bool isRowDisplayed(const RowSnapshot &row)
{
	if (row.kind == ProviderKind::Primary)
		return true;
	return row.hasSession && isRunning(row.state);
}

const RowSnapshot *lastFailure(const std::vector<RowSnapshot> &all)
{
	const RowSnapshot *best = nullptr;
	for (const RowSnapshot &r : all) {
		if (r.kind == ProviderKind::Primary || !r.hasSession)
			continue;
		if (r.state != OutputState::Disconnected && r.state != OutputState::Error)
			continue;
		if (!best || r.endNs > best->endNs)
			best = &r;
	}
	return best;
}

std::vector<RowSnapshot> displayedRows(std::vector<RowSnapshot> rows)
{
	rows.erase(std::remove_if(rows.begin(), rows.end(), [](const RowSnapshot &r) { return !isRowDisplayed(r); }),
		   rows.end());
	return rows;
}

Health systemHealth(const SystemSnapshot &system)
{
	Health h = Health::Inactive;
	h = worst(h, system.fpsHealth);
	h = worst(h, system.renderHealth);
	h = worst(h, system.renderLagHealth);
	h = worst(h, system.encodeLagHealth);
	return h;
}

SummarySnapshot computeSummary(const std::vector<RowSnapshot> &rows, const SystemSnapshot &system)
{
	SummarySnapshot s;
	double kbpsSum = 0.0;
	int kbpsCount = 0;
	uint64_t droppedSum = 0;
	uint64_t framesSum = 0;
	bool anyDropValue = false;
	Health overall = Health::Inactive;

	for (const RowSnapshot &r : rows) {
		s.rows++;
		const bool running = isRunning(r.state);
		if (running)
			s.running++;

		switch (r.state) {
		case OutputState::Live:
			s.live++;
			break;
		case OutputState::Connecting:
			s.connecting++;
			break;
		case OutputState::Reconnecting:
			s.reconnecting++;
			break;
		case OutputState::Disconnected:
		case OutputState::Error:
			s.failed++;
			break;
		default:
			break;
		}

		if (r.health == Health::Warning)
			s.warnings++;
		else if (r.health == Health::Bad)
			s.problems++;
		overall = worst(overall, r.health);

		if (r.state == OutputState::Live || r.state == OutputState::Reconnecting ||
		    r.state == OutputState::Stopping) {
			if (r.kbps.has()) {
				kbpsSum += r.kbps.value();
				kbpsCount++;
			} else {
				s.unmeasured++;
			}
		}

		if (running) {
			if (r.dropped.has() && r.totalFrames.has()) {
				droppedSum += r.dropped.value();
				framesSum += r.totalFrames.value();
				anyDropValue = true;
			} else if (r.dropped.reason() == Unavailable::NotSupported) {
				s.dropUnsupported++;
			}
		}
	}

	const int flowing = s.live + s.reconnecting;
	if (kbpsCount > 0)
		s.sumKbps = Metric<double>::of(kbpsSum);
	else
		s.sumKbps = Metric<double>::na(flowing > 0 ? Unavailable::NotMeasured : Unavailable::NotActive);

	if (anyDropValue) {
		s.droppedSum = Metric<uint64_t>::of(droppedSum);
		s.framesSum = Metric<uint64_t>::of(framesSum);
		s.dropPct = framesSum > 0 ? Metric<double>::of((double)droppedSum * 100.0 / (double)framesSum)
					  : Metric<double>::na(Unavailable::NotMeasured);
	} else {
		const Unavailable reason = s.running == 0      ? Unavailable::NotActive
					   : s.dropUnsupported > 0 ? Unavailable::NotSupported
								   : Unavailable::NotMeasured;
		s.droppedSum = Metric<uint64_t>::na(reason);
		s.framesSum = Metric<uint64_t>::na(reason);
		s.dropPct = Metric<double>::na(reason);
	}

	s.systemHealth = systemHealth(system);
	// System problems only affect *streaming* health while something runs.
	if (s.running > 0)
		overall = worst(overall, s.systemHealth);
	s.overall = overall;
	return s;
}

} // namespace msd
