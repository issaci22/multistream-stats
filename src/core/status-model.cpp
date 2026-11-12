// SPDX-License-Identifier: GPL-2.0-or-later
#include "status-model.hpp"
#include "observation.hpp"

namespace msd {

bool isRunning(OutputState s)
{
	switch (s) {
	case OutputState::Connecting:
	case OutputState::Live:
	case OutputState::Reconnecting:
	case OutputState::Stopping:
		return true;
	default:
		return false;
	}
}

bool isFailure(OutputState s)
{
	return s == OutputState::Reconnecting || s == OutputState::Disconnected || s == OutputState::Error;
}

OutputState stateForStopCode(int code)
{
	if (code == StopCode::Success)
		return OutputState::Stopped;
	if (code == StopCode::Disconnected)
		return OutputState::Disconnected;
	return OutputState::Error;
}

Health stateHealth(OutputState s)
{
	switch (s) {
	case OutputState::Live:
		return Health::Good;
	case OutputState::Connecting:
	case OutputState::Stopping:
		return Health::Warning;
	case OutputState::Reconnecting:
	case OutputState::Disconnected:
	case OutputState::Error:
		return Health::Bad;
	case OutputState::Idle:
	case OutputState::Stopped:
	default:
		return Health::Inactive;
	}
}

Health percentHealth(const Metric<double> &pct)
{
	if (!pct.has())
		return Health::Inactive;
	if (pct.value() > Thresholds::PercentDanger)
		return Health::Bad;
	if (pct.value() > Thresholds::PercentWarning)
		return Health::Warning;
	return Health::Good;
}

Health congestionHealth(const Metric<double> &congestion)
{
	if (!congestion.has())
		return Health::Inactive;
	if (congestion.value() > Thresholds::CongestionDanger)
		return Health::Bad;
	if (congestion.value() > Thresholds::CongestionWarning)
		return Health::Warning;
	return Health::Good;
}

Health fpsHealth(const Metric<double> &fps, double targetFps)
{
	if (!fps.has() || targetFps <= 0.0)
		return Health::Inactive;
	if (fps.value() < targetFps * Thresholds::FpsDangerRatio)
		return Health::Bad;
	if (fps.value() < targetFps * Thresholds::FpsWarningRatio)
		return Health::Warning;
	return Health::Good;
}

Health renderTimeHealth(const Metric<double> &renderMs, double targetFps)
{
	if (!renderMs.has() || targetFps <= 0.0)
		return Health::Inactive;
	const double frameMs = 1000.0 / targetFps;
	if (renderMs.value() > frameMs)
		return Health::Bad;
	if (renderMs.value() > frameMs * Thresholds::RenderWarningRatio)
		return Health::Warning;
	return Health::Good;
}

} // namespace msd
