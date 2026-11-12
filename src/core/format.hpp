// SPDX-License-Identifier: GPL-2.0-or-later
//
// Number formatting matching OBS's native Stats dock conventions
// (kb/s below 10 000, Mb/s above; percentages with one decimal; FPS with two).
// All functions take raw values; N/A handling is done by the caller so that a
// missing value can never be formatted as "0".
#pragma once

#include <cstdint>
#include <string>

namespace msd::fmt {

std::string kbps(double kbps);              // "6012 kb/s", "19.84 Mb/s"
std::string percent(double pct, int decimals = 1); // "2.9%"
std::string frames(uint64_t part, uint64_t total, double pct); // "720 / 25142 (2.9%)"
std::string framesCompact(uint64_t part, double pct);           // "720 (2.9%)" for narrow docks
std::string count(uint64_t n);              // "25142"
std::string bytes(uint64_t bytes);          // "512.3 MiB", "3.21 GiB"
std::string duration(uint64_t ns);          // "01:12:44"
std::string fps(double fps);                // "60.00"
std::string milliseconds(double ms, int decimals = 1); // "1.2 ms"
std::string resolution(uint32_t w, uint32_t h);  // "1920x1080"

} // namespace msd::fmt
