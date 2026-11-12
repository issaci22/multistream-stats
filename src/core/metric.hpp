// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <optional>

namespace msd {

// Why a metric has no value. A metric without a value is always displayed as
// "N/A" (never as 0) and the reason is surfaced in the tooltip.
enum class Unavailable {
	None,         // value is present
	NotSupported, // the output type does not report this metric
	NotActive,    // output is not running, value only exists while active
	NotMeasured,  // not enough samples yet (e.g. bitrate needs two samples)
	Unknown,      // could not be determined (e.g. start before monitoring began)
};

template<typename T> class Metric {
public:
	Metric() = default;

	static Metric of(T v)
	{
		Metric m;
		m.value_ = v;
		m.reason_ = Unavailable::None;
		return m;
	}

	static Metric na(Unavailable reason)
	{
		Metric m;
		m.reason_ = reason == Unavailable::None ? Unavailable::Unknown : reason;
		return m;
	}

	bool has() const { return value_.has_value(); }
	const T &value() const { return *value_; }
	T valueOr(T fallback) const { return value_.value_or(fallback); }
	Unavailable reason() const { return reason_; }

	bool operator==(const Metric &o) const { return value_ == o.value_ && reason_ == o.reason_; }

private:
	std::optional<T> value_;
	Unavailable reason_ = Unavailable::NotMeasured;
};

} // namespace msd
