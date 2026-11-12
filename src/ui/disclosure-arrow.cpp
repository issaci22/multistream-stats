// SPDX-License-Identifier: GPL-2.0-or-later
#include "disclosure-arrow.hpp"

#include <QPainter>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOptionFocusRect>

namespace msdui {

DisclosureArrow::DisclosureArrow(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(true);
	setChecked(true);
	setFocusPolicy(Qt::TabFocus);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setObjectName(QStringLiteral("msd-disclosure")); // stable name for the isolated UI tests
}

QSize DisclosureArrow::sizeHint() const
{
	const int h = fontMetrics().height();
	return {h, h};
}

void DisclosureArrow::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const double half = qMax(3.0, fontMetrics().height() * 0.22);
	const QPointF c = QRectF(rect()).center();
	QPolygonF tri;
	if (isChecked())
		tri << QPointF(c.x() - half, c.y() - half * 0.5) << QPointF(c.x() + half, c.y() - half * 0.5)
		    << QPointF(c.x(), c.y() + half * 0.6);
	else
		tri << QPointF(c.x() - half * 0.5, c.y() - half) << QPointF(c.x() - half * 0.5, c.y() + half)
		    << QPointF(c.x() + half * 0.6, c.y());

	QColor color = palette().color(QPalette::WindowText);
	if (!underMouse() && !hasFocus())
		color.setAlphaF(0.75);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPolygon(tri);

	if (hasFocus()) {
		QStyleOptionFocusRect opt;
		opt.initFrom(this);
		style()->drawPrimitive(QStyle::PE_FrameFocusRect, &opt, &p, this);
	}
}

} // namespace msdui
