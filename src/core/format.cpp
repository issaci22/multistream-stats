// SPDX-License-Identifier: GPL-2.0-or-later
#include "format.hpp"

#include <cmath>
#include <cstdio>

namespace msd::fmt {

namespace {
std::string printf_str(const char *f, double v)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), f, v);
	return buf;
}
} // namespace

std::string kbps(double kbps)
{
	if (!std::isfinite(kbps) || kbps < 0.0)
		kbps = 0.0;
	if (kbps >= 10'000.0)
		return printf_str("%.2f Mb/s", kbps / 1000.0);
	return printf_str("%.0f kb/s", kbps);
}

std::string percent(double pct, int decimals)
{
	char f[16];
	std::snprintf(f, sizeof(f), "%%.%df%%%%", decimals < 0 ? 0 : decimals);
	return printf_str(f, pct);
}

std::string count(uint64_t n)
{
	return std::to_string(n);
}

std::string frames(uint64_t part, uint64_t total, double pct)
{
	return count(part) + " / " + count(total) + " (" + percent(pct) + ")";
}

std::string framesCompact(uint64_t part, double pct)
{
	return count(part) + " (" + percent(pct) + ")";
}

std::string bytes(uint64_t b)
{
	const double mib = (double)b / (1024.0 * 1024.0);
	if (mib >= 1024.0)
		return printf_str("%.2f GiB", mib / 1024.0);
	return printf_str("%.1f MiB", mib);
}

std::string duration(uint64_t ns)
{
	const uint64_t total = ns / 1'000'000'000ULL;
	const uint64_t h = total / 3600, m = (total / 60) % 60, s = total % 60;
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu", (unsigned long long)h, (unsigned long long)m,
		      (unsigned long long)s);
	return buf;
}

std::string fps(double v)
{
	return printf_str("%.2f", v);
}

std::string milliseconds(double ms, int decimals)
{
	char f[16];
	std::snprintf(f, sizeof(f), "%%.%df ms", decimals < 0 ? 0 : decimals);
	return printf_str(f, ms);
}

std::string resolution(uint32_t w, uint32_t h)
{
	return std::to_string(w) + "x" + std::to_string(h);
}

} // namespace msd::fmt
