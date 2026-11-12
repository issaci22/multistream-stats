// SPDX-License-Identifier: GPL-2.0-or-later
#include "stat-grid.hpp"

#include <core/layout-math.hpp>

#include <QEvent>
#include <QStyle>

#include <algorithm>

namespace msdui {

namespace {
// Only rows hidden on purpose (setRowVisible(false)) are skipped. Children of
// a not-yet-shown widget also report isHidden(), but they will be shown with it.
bool explicitlyHidden(const QWidget *w)
{
	return w->isHidden() && w->testAttribute(Qt::WA_WState_ExplicitShowHide);
}

// Descriptive text (encoder names, hosts, errors) may elide; for deciding how
// many columns fit it counts with at most this many average characters.
constexpr int kTextColumnChars = 24;
} // namespace

StatGrid::StatGrid(QWidget *parent) : QWidget(parent)
{
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);
}

ElidedLabel *StatGrid::addRow(const QString &label, const QString &tooltip, ElidedLabel::Role valueRole)
{
	auto *name = new ElidedLabel(this, ElidedLabel::Role::Label);
	name->setFullText(label);
	name->setBaseToolTip(tooltip);
	name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	auto *value = new ElidedLabel(this, valueRole);
	value->setProperty("msdTip", tooltip);
	value->setBaseToolTip(tooltip);
	value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	rows_.push_back({name, value});
	labels_[value] = name;
	invalidate();
	if (isVisible()) // rows are normally added before the first show
		relayout();
	return value;
}

void StatGrid::setRowVisible(ElidedLabel *value, bool visible)
{
	auto it = labels_.find(value);
	if (it == labels_.end())
		return;
	if (explicitlyHidden(value) != visible)
		return; // already in the requested state
	it->second->setVisible(visible);
	value->setVisible(visible);
	invalidate();
	relayout();
}

void StatGrid::setMaxColumns(int columns)
{
	columns = std::max(1, columns);
	if (columns == maxColumns_)
		return;
	maxColumns_ = columns;
	invalidate();
	relayout();
}

int StatGrid::hSpacing() const
{
	if (hSpacing_ < 0) {
		int s = style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, this);
		if (s < 0)
			s = style()->layoutSpacing(QSizePolicy::Label, QSizePolicy::Label, Qt::Horizontal, nullptr, this);
		hSpacing_ = s < 0 ? 6 : s;
	}
	return hSpacing_;
}

int StatGrid::vSpacing() const
{
	if (vSpacing_ < 0) {
		int s = style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, this);
		if (s < 0)
			s = style()->layoutSpacing(QSizePolicy::Label, QSizePolicy::Label, Qt::Vertical, nullptr, this);
		vSpacing_ = s < 0 ? 6 : s;
	}
	return vSpacing_;
}

void StatGrid::invalidate()
{
	hfwWidth_ = -1;
	updateGeometry();
}

StatGrid::Rows StatGrid::visibleRows() const
{
	Rows out;
	out.reserve(rows_.size());
	for (const Row &r : rows_)
		if (!explicitlyHidden(r.value))
			out.push_back(&r);
	return out;
}

int StatGrid::rowHeight(const Row &row) const
{
	return std::max(row.label->sizeHint().height(), row.value->sizeHint().height());
}

StatGrid::Metrics StatGrid::measure(const Rows &rows, size_t begin, size_t end) const
{
	Metrics m;
	end = std::min(end, rows.size());
	for (size_t i = begin; i < end; ++i) {
		const Row &r = *rows[i];
		m.labelNatural = std::max(m.labelNatural, r.label->sizeHint().width());
		m.labelMinimum = std::max(m.labelMinimum, r.label->minimumSizeHint().width());
		m.valueHint = std::max(m.valueHint, r.value->sizeHint().width());
		m.valueMinimum = std::max(m.valueMinimum, r.value->minimumSizeHint().width());
		if (r.value->role() == ElidedLabel::Role::Value) {
			m.valueFull = std::max(m.valueFull, r.value->sizeHint().width());
			m.valueCompact = std::max(m.valueCompact, r.value->minimumSizeHint().width());
		}
		m.height += rowHeight(r);
	}
	if (end > begin + 1)
		m.height += (int)(end - begin - 1) * vSpacing();
	return m;
}

int StatGrid::columnsFor(int width, const Rows &rows) const
{
	if (maxColumns_ <= 1 || rows.size() <= 1)
		return 1;
	const Metrics m = measure(rows, 0, rows.size());
	// Every column must hold any row at its natural label width with the
	// value in full (descriptive text counted up to kTextColumnChars).
	const int textCap = fontMetrics().averageCharWidth() * kTextColumnChars;
	const int value = std::max(m.valueFull, std::min(m.valueHint, textCap));
	const int pair = m.labelNatural + hSpacing() + value;
	return msd::layout::gridColumns(width, pair, columnGap(), (int)rows.size(), maxColumns_);
}

int StatGrid::heightFor(const Rows &rows, int columns) const
{
	const int per = msd::layout::rowsPerColumn((int)rows.size(), columns);
	int height = 0;
	for (size_t b = 0; b < rows.size(); b += (size_t)per)
		height = std::max(height, measure(rows, b, b + (size_t)per).height);
	return height;
}

QSize StatGrid::sizeHint() const
{
	// Descriptive text (Role::Text) may elide, so it does not claim more than
	// kTextColumnChars of natural width; measured values always count in full.
	const Rows rows = visibleRows();
	const Metrics m = measure(rows, 0, rows.size());
	const int textCap = fontMetrics().averageCharWidth() * kTextColumnChars;
	const int value = std::max(m.valueFull, std::min(m.valueHint, textCap));
	return {m.labelNatural + hSpacing() + value, m.height};
}

QSize StatGrid::minimumSizeHint() const
{
	// Width: one column at its minimum (1.0.1). Height: the real height comes
	// from heightForWidth(); a single-column minimum here would keep the
	// enclosing scroll area as tall as the one-column layout.
	const Rows rows = visibleRows();
	const Metrics m = measure(rows, 0, rows.size());
	const int oneRow = rows.empty() ? 0 : rowHeight(*rows.front());
	return {m.labelMinimum + hSpacing() + m.valueMinimum, oneRow};
}

int StatGrid::heightForWidth(int width) const
{
	if (width == hfwWidth_)
		return hfwHeight_;
	const Rows rows = visibleRows();
	hfwHeight_ = heightFor(rows, columnsFor(width, rows));
	hfwWidth_ = width;
	return hfwHeight_;
}

bool StatGrid::event(QEvent *event)
{
	// A child label's updateGeometry() (its widest text grew) posts a
	// LayoutRequest to this widget because it has no QLayout.
	if (event->type() == QEvent::LayoutRequest) {
		invalidate();
		relayout();
		return true;
	}
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::Polish) {
		hSpacing_ = vSpacing_ = -1; // theme changed: re-read spacing
		invalidate();
	}
	return QWidget::event(event);
}

void StatGrid::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	relayout();
}

void StatGrid::relayout()
{
	const Rows rows = visibleRows();
	const int w = width();
	const int columns = columnsFor(w, rows);
	if (columns != columns_) {
		columns_ = columns;
		updateGeometry(); // height changes with the column count
	}
	if (columns == 1) {
		layoutColumn(rows, 0, rows.size(), 0, w);
		return;
	}
	// Each column is as wide as its own content needs (at most an equal share),
	// so columns sit next to each other and spare width stays at the end.
	const int gap = columnGap();
	const int share = (w - gap * (columns - 1)) / columns;
	const int per = msd::layout::rowsPerColumn((int)rows.size(), columns);
	int x = 0;
	for (int c = 0; c < columns; ++c) {
		const size_t b = (size_t)(c * per);
		if (b >= rows.size())
			break;
		const size_t e = std::min(rows.size(), b + (size_t)per);
		const Metrics m = measure(rows, b, e);
		const int colWidth = std::min(share, m.labelNatural + hSpacing() + m.valueHint);
		layoutColumn(rows, b, e, x, colWidth);
		x += colWidth + gap;
	}
}

void StatGrid::layoutColumn(const Rows &rows, size_t begin, size_t end, int x, int w)
{
	const Metrics m = measure(rows, begin, end);
	const int sp = hSpacing();

	// Label column, in order of preference:
	//  1. natural width if every measured value fits in full next to it;
	//  2. labels shrink to make room for full values, but not below a
	//     comfortable 40% of the column (so captions stay readable);
	//  3. beyond that, values switch to their compact form (the key number
	//     stays, the rest moves to the tooltip) and labels keep 40%;
	//  4. if even that does not fit, labels shrink to their minimum.
	const int labelFloor = std::min(m.labelMinimum, m.labelNatural);
	const int labelComfort = std::min(m.labelNatural, std::max(labelFloor, w * 2 / 5));
	int labelCol = std::min(m.labelNatural, w - sp - m.valueFull);
	if (labelCol < labelComfort)
		labelCol = std::min(labelComfort, w - sp - m.valueCompact);
	labelCol = std::max(labelCol, labelFloor);
	// Values must keep at least their compact width even if that means the
	// labels get less than their minimum (only below the grid's minimum width).
	labelCol = std::min(labelCol, std::max(0, w - sp - m.valueMinimum));
	labelCol = std::max(labelCol, 0);

	const int valueX = labelCol + sp;
	const int valueW = std::max(0, w - valueX);
	const bool rtl = layoutDirection() == Qt::RightToLeft;

	int y = 0;
	const int vsp = vSpacing();
	for (size_t i = begin; i < end; ++i) {
		const Row &r = *rows[i];
		const int h = rowHeight(r);
		QRect labelRect(x, y, labelCol, h);
		QRect valueRect(x + valueX, y, valueW, h);
		if (rtl) {
			labelRect = QStyle::visualRect(Qt::RightToLeft, rect(), labelRect);
			valueRect = QStyle::visualRect(Qt::RightToLeft, rect(), valueRect);
		}
		r.label->setGeometry(labelRect);
		r.value->setGeometry(valueRect);
		y += h + vsp;
	}
}

} // namespace msdui
