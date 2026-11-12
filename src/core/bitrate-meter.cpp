// SPDX-License-Identifier: GPL-2.0-or-later
#include "bitrate-meter.hpp"

#include <algorithm>

namespace msd {

// ---------------------------------------------------------------------------
void CounterAccumulator::reset()
{
	offset_ = 0;
	last_ = 0;
	has_ = false;
}

uint64_t CounterAccumulator::update(uint64_t raw)
{
	if (has_ && raw < last_)
		offset_ += last_; // counter was reset by the output (e.g. reconnect)
	last_ = raw;
	has_ = true;
	return offset_ + last_;
}

// ---------------------------------------------------------------------------
void BitrateMeter::reset()
{
	points_.clear();
}

void BitrateMeter::addSample(uint64_t timestampNs, uint64_t bytes)
{
	if (!points_.empty()) {
		const Point &last = points_.back();
		if (timestampNs <= last.ns)
			return; // clock did not advance; ignore duplicate sample
		if (bytes < last.bytes)
			points_.clear(); // not monotonic: restart measurement
	}
	points_.push_back({timestampNs, bytes});

	// Keep the newest point plus everything inside the window.
	while (points_.size() > 2 && timestampNs - points_[1].ns >= windowNs_)
		points_.pop_front();
}

Metric<double> BitrateMeter::currentKbps() const
{
	if (points_.size() < 2)
		return Metric<double>::na(Unavailable::NotMeasured);

	const Point &first = points_.front();
	const Point &last = points_.back();
	const uint64_t dtNs = last.ns - first.ns;
	if (dtNs < 10'000'000ULL) // same guard as native dock (< 0.01 s)
		return Metric<double>::na(Unavailable::NotMeasured);

	const long double bits = (long double)(last.bytes - first.bytes) * 8.0L;
	const long double seconds = (long double)dtNs / 1'000'000'000.0L;
	return Metric<double>::of((double)(bits / seconds / 1000.0L));
}

Metric<double> averageKbps(uint64_t bytes, uint64_t elapsedNs)
{
	if (elapsedNs < 1'000'000'000ULL) // less than a second: not meaningful yet
		return Metric<double>::na(Unavailable::NotMeasured);
	const long double bits = (long double)bytes * 8.0L;
	const long double seconds = (long double)elapsedNs / 1'000'000'000.0L;
	return Metric<double>::of((double)(bits / seconds / 1000.0L));
}

// ---------------------------------------------------------------------------
void CongestionSmoother::reset()
{
	window_.clear();
	last_ = 0.0f;
	hasLast_ = false;
}

void CongestionSmoother::addSample(float congestion)
{
	congestion = std::clamp(congestion, 0.0f, 1.0f);
	float avg = hasLast_ ? (congestion + last_) * 0.5f : congestion;
	if (avg < congestion)
		avg = congestion;
	avg = std::min(avg, 1.0f);
	last_ = congestion;
	hasLast_ = true;

	window_.push_back(avg);
	while (window_.size() > 4)
		window_.pop_front();
}

Metric<double> CongestionSmoother::value() const
{
	if (window_.empty())
		return Metric<double>::na(Unavailable::NotMeasured);
	double sum = 0.0;
	for (float v : window_)
		sum += v;
	return Metric<double>::of(sum / (double)window_.size());
}

// ---------------------------------------------------------------------------
void RecentDropWindow::reset()
{
	points_.clear();
}

void RecentDropWindow::addSample(uint64_t timestampNs, uint64_t dropped, uint64_t total)
{
	if (!points_.empty()) {
		const Point &last = points_.back();
		if (timestampNs <= last.ns)
			return;
		if (dropped < last.dropped || total < last.total)
			points_.clear();
	}
	points_.push_back({timestampNs, dropped, total});
	while (points_.size() > 2 && timestampNs - points_[1].ns >= windowNs_)
		points_.pop_front();
}

Metric<double> RecentDropWindow::recentPercent() const
{
	if (points_.size() < 2)
		return Metric<double>::na(Unavailable::NotMeasured);
	const Point &first = points_.front();
	const Point &last = points_.back();
	const uint64_t frames = last.total - first.total;
	if (frames == 0)
		return Metric<double>::na(Unavailable::NotMeasured);
	return Metric<double>::of((double)(last.dropped - first.dropped) * 100.0 / (double)frames);
}

uint64_t RecentDropWindow::recentDropped() const
{
	if (points_.size() < 2)
		return 0;
	return points_.back().dropped - points_.front().dropped;
}

} // namespace msd
