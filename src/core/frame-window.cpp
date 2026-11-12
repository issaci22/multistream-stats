// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame-window.hpp"

namespace msd {

FrameCounts FrameWindow::sample(uint32_t total, uint32_t affected, bool *rebased)
{
	bool rebase = false;
	if (!have_ || total < firstTotal_ || affected < firstAffected_) {
		baseline(total, affected);
		rebase = true;
	}
	if (rebased)
		*rebased = rebase;

	const uint32_t t = total - firstTotal_;
	const uint32_t a = affected - firstAffected_;
	FrameCounts c;
	c.total = Metric<uint64_t>::of(t);
	c.affected = Metric<uint64_t>::of(a);
	c.percent = t > 0 ? Metric<double>::of((double)a * 100.0 / (double)t) : Metric<double>::na(Unavailable::NotMeasured);
	return c;
}

} // namespace msd
