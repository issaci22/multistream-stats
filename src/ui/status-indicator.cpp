// SPDX-License-Identifier: GPL-2.0-or-later
#include "status-indicator.hpp"
#include "theme.hpp"

#include <QPainter>

namespace msdui {

StatusIndicator::StatusIndicator(QWidget *parent) : QLabel(parent)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setClasses(this, healthClass(health_));
}

void StatusIndicator::setHealth(msd::Health health)
{
	if (health == health_ && !property("class").toString().isEmpty())
		return;
	health_ = health;
	setClasses(this, healthClass(health));
	update();
}

QSize StatusIndicator::sizeHint() const
{
	const int side = qMax(6, (fontMetrics().height() * 6) / 10);
	return {side + 2, fontMetrics().height()};
}

QSize StatusIndicator::minimumSizeHint() const
{
	return sizeHint();
}

void StatusIndicator::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	const int side = qMax(6, (fontMetrics().height() * 6) / 10);
	const QRect r((width() - side) / 2, (height() - side) / 2, side, side);
	const QColor c = palette().color(QPalette::WindowText);
	if (health_ == msd::Health::Inactive) {
		// Inactive: outlined square, like a disabled indicator.
		p.setPen(c);
		p.setBrush(Qt::NoBrush);
		p.drawRect(r.adjusted(0, 0, -1, -1));
	} else {
		p.fillRect(r, c);
	}
}

} // namespace msdui
