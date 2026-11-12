// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "metric.hpp"

namespace msd {

enum class OutputState {
	Idle,         // output exists / is configured but not running
	Connecting,   // start requested, not yet connected
	Live,         // connected and sending
	Reconnecting, // connection lost, libobs is retrying
	Stopping,     // stop requested, flushing
	Stopped,      // ended normally
	Disconnected, // ended because the connection was lost (OBS_OUTPUT_DISCONNECTED)
	Error,        // ended / failed with an error code
};

// Display severity, mapped to OBS theme classes:
//   Inactive -> text-muted (grey), Good -> text-success (green),
//   Warning -> text-warning (yellow), Bad -> text-danger (red)
enum class Health { Inactive = 0, Good = 1, Warning = 2, Bad = 3 };

inline Health worst(Health a, Health b)
{
	return (int)a >= (int)b ? a : b;
}

// Thresholds copied from OBS 33.0.1 (OBSBasicStats / OBSBasicStatusBar).
namespace Thresholds {
constexpr double PercentWarning = 1.0;     // dropped / skipped / missed > 1 %
constexpr double PercentDanger = 5.0;      // > 5 %
constexpr double FpsWarningRatio = 0.95;   // FPS < 95 % of target
constexpr double FpsDangerRatio = 0.80;    // FPS < 80 % of target
constexpr double RenderWarningRatio = 0.75; // render time > 75 % of frame interval
constexpr double CongestionWarning = 0.3333; // status bar "mediocre"
constexpr double CongestionDanger = 0.6667;  // status bar "bad"
} // namespace Thresholds

bool isRunning(OutputState s);    // Connecting / Live / Reconnecting / Stopping
bool isFailure(OutputState s);    // Reconnecting / Disconnected / Error
OutputState stateForStopCode(int code);
Health stateHealth(OutputState s);

Health percentHealth(const Metric<double> &pct);
Health congestionHealth(const Metric<double> &congestion);
Health fpsHealth(const Metric<double> &fps, double targetFps);
Health renderTimeHealth(const Metric<double> &renderMs, double targetFps);

} // namespace msd
