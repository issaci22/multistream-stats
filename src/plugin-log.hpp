// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <util/base.h>

#include <string>
#include <utility>

namespace msd {

inline constexpr const char *kLogPrefix = "[multistream-stats] ";

template<typename... Args> inline void log(int level, const char *format, Args &&...args)
{
	std::string f = kLogPrefix;
	f += format;
	blog(level, f.c_str(), std::forward<Args>(args)...);
}

} // namespace msd
