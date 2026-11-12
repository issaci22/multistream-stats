// SPDX-License-Identifier: GPL-2.0-or-later
//
// Small status square (like the OBS status bar's stream indicator). The fill
// color is the label's theme foreground color for its health class, so it
// follows the active OBS theme. Sized from the font so it scales with DPI and
// the user's font-size setting.
#pragma once

#include <core/status-model.hpp>

#include <QLabel>

namespace msdui {

class StatusIndicator : public QLabel {
public:
	explicit StatusIndicator(QWidget *parent = nullptr);

	void setHealth(msd::Health health);
	msd::Health health() const { return health_; }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	msd::Health health_ = msd::Health::Inactive;
};

} // namespace msdui
