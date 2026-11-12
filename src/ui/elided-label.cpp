// SPDX-License-Identifier: GPL-2.0-or-later
#include "elided-label.hpp"

#include <QEvent>
#include <QPainter>
#include <QStyle>

#include <algorithm>

namespace msdui {

ElidedLabel::ElidedLabel(QWidget *parent, Role role) : QLabel(parent), role_(role)
{
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	setTextFormat(Qt::PlainText);
}

void ElidedLabel::setRole(Role role)
{
	if (role == role_)
		return;
	role_ = role;
	updateGeometry();
	updateToolTip();
	update();
}

void ElidedLabel::setFullText(const QString &text, const QString &compact)
{
	// A compact form identical to the full text is no fallback at all.
	const QString c = compact == text ? QString() : compact;
	if (text == full_ && c == compact_)
		return;
	full_ = text;
	compact_ = c;
	setAccessibleName(text);
	// Grow-only size hints: values that change every second (bitrate,
	// duration...) only trigger a re-layout when they become wider than ever
	// before, not on every digit change.
	const QFontMetrics fm = fontMetrics();
	const int width = fm.horizontalAdvance(full_);
	const int cwidth = compact_.isEmpty() ? width : fm.horizontalAdvance(compact_);
	if (width > textWidth_ || cwidth > compactWidth_) {
		textWidth_ = std::max(textWidth_, width);
		compactWidth_ = std::max(compactWidth_, cwidth);
		updateGeometry();
	}
	updateToolTip();
	update();
}

void ElidedLabel::setBaseToolTip(const QString &tip)
{
	if (tip == baseTip_)
		return;
	baseTip_ = tip;
	updateToolTip();
}

ElidedLabel::Mode ElidedLabel::mode() const
{
	const int avail = contentsRect().width();
	const QFontMetrics fm = fontMetrics();
	if (fm.horizontalAdvance(full_) <= avail)
		return Mode::Full;
	if (!compact_.isEmpty() && fm.horizontalAdvance(compact_) <= avail)
		return Mode::Compact;
	// A Value whose compact text does not fit is still drawn in compact form
	// (clipped only if a layout ignored its minimum size); it is never elided.
	if (role_ == Role::Value && !compact_.isEmpty())
		return Mode::Compact;
	return role_ == Role::Value ? Mode::Full : Mode::Elided;
}

QString ElidedLabel::shownText() const
{
	switch (mode()) {
	case Mode::Full:
		return full_;
	case Mode::Compact:
		return compact_;
	case Mode::Elided:
	default:
		return fontMetrics().elidedText(compact_.isEmpty() ? full_ : compact_, Qt::ElideRight,
						contentsRect().width());
	}
}

int ElidedLabel::minimumTextWidth() const
{
	const QFontMetrics fm = fontMetrics();
	switch (role_) {
	case Role::Value:
		return compactWidth_; // never less than the (compact) value itself
	case Role::Text:
		return std::min(textWidth_, fm.averageCharWidth() * 6 + fm.horizontalAdvance(QStringLiteral("…")));
	case Role::Label:
	default:
		return std::min(textWidth_, fm.averageCharWidth() * 3 + fm.horizontalAdvance(QStringLiteral("…")));
	}
}

QSize ElidedLabel::sizeHint() const
{
	const QMargins m = contentsMargins();
	return {textWidth_ + m.left() + m.right() + 2, fontMetrics().height() + m.top() + m.bottom()};
}

QSize ElidedLabel::minimumSizeHint() const
{
	const QMargins m = contentsMargins();
	return {minimumTextWidth() + m.left() + m.right() + (role_ == Role::Value ? 2 : 0),
		fontMetrics().height() + m.top() + m.bottom()};
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
	QLabel::resizeEvent(event);
	updateToolTip();
}

void ElidedLabel::remeasure()
{
	const QFontMetrics fm = fontMetrics();
	textWidth_ = fm.horizontalAdvance(full_);
	compactWidth_ = compact_.isEmpty() ? textWidth_ : fm.horizontalAdvance(compact_);
	updateGeometry();
}

void ElidedLabel::changeEvent(QEvent *event)
{
	QLabel::changeEvent(event);
	// Font/style changes (theme switch, DPI change) invalidate measured widths.
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		remeasure();
		updateToolTip();
	}
}

void ElidedLabel::updateToolTip()
{
	QString tip = baseTip_;
	if (contentsRect().width() > 0 && mode() != Mode::Full && !full_.isEmpty()) {
		if (!tip.isEmpty())
			tip += QStringLiteral("\n\n");
		tip += full_;
	}
	if (toolTip() != tip)
		setToolTip(tip);
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	const QRect r = contentsRect();
	style()->drawItemText(&p, r, (int)(alignment() | Qt::TextSingleLine), palette(), isEnabled(), shownText(),
			      foregroundRole());
}

} // namespace msdui
