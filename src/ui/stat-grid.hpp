// SPDX-License-Identifier: GPL-2.0-or-later
//
// Two-column "label  value" grid, the layout used by OBS's native Stats dock.
//
// 1.0.1: lays its rows out itself instead of using QGridLayout, because
// QGridLayout takes missing width equally from both columns, which squeezed
// the values while long labels kept their full width. Priority here:
//   1. labels shrink (elide) first, down to a few characters;
//   2. only then do values switch to their compact form;
//   3. values are never elided: the grid's minimum width includes every
//      visible value's compact width, so a too-narrow dock scrolls instead of
//      cutting values off.
// With enough width the result is identical to the previous grid: label column
// as wide as the widest label, values left-aligned after it.
//
// 1.1.0: when the grid is wide enough for several label/value pairs side by
// side (each at its natural width, values in full), rows flow into up to
// maxColumns() columns, filled column by column like the native Stats dock's
// top grid. Its height then depends on its width (heightForWidth). With one
// column the 1.0.1 algorithm above applies unchanged.
#pragma once

#include "elided-label.hpp"

#include <QWidget>

#include <map>
#include <vector>

namespace msdui {

class StatGrid : public QWidget {
public:
	explicit StatGrid(QWidget *parent = nullptr);

	// Adds a row and returns its value label. The tooltip explains the metric
	// and is shown on both the label and the value.
	ElidedLabel *addRow(const QString &label, const QString &tooltip,
			    ElidedLabel::Role valueRole = ElidedLabel::Role::Value);
	void setRowVisible(ElidedLabel *value, bool visible);

	void setMaxColumns(int columns);
	int maxColumns() const { return maxColumns_; }
	int columnCount() const { return columns_; } // as currently laid out

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;

protected:
	bool event(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	struct Row {
		ElidedLabel *label;
		ElidedLabel *value;
	};
	struct Metrics {
		int labelNatural = 0;   // widest visible label
		int labelMinimum = 0;   // labels never shrink below this (or their own width)
		int valueHint = 0;      // widest visible value (full text)
		int valueFull = 0;      // widest visible Role::Value value (full text)
		int valueCompact = 0;   // widest visible Role::Value value (compact text)
		int valueMinimum = 0;   // widest visible value minimum (any role)
		int height = 0;
	};
	using Rows = std::vector<const Row *>;
	Rows visibleRows() const;
	Metrics measure(const Rows &rows, size_t begin, size_t end) const;
	int rowHeight(const Row &row) const;
	int columnsFor(int width, const Rows &rows) const;
	int heightFor(const Rows &rows, int columns) const;
	int hSpacing() const;
	int vSpacing() const;
	int columnGap() const { return hSpacing() * 4; }
	void invalidate();
	void relayout();
	void layoutColumn(const Rows &rows, size_t begin, size_t end, int x, int width);

	std::vector<Row> rows_;
	std::map<ElidedLabel *, ElidedLabel *> labels_;
	int maxColumns_ = 1;
	int columns_ = 1;
	// Style spacing, cached: with OBS's style-sheet themes every pixelMetric()
	// call resolves style rules, which made each layout pass expensive.
	mutable int hSpacing_ = -1;
	mutable int vSpacing_ = -1;
	// Last heightForWidth() answer (layouts ask repeatedly for the same width).
	mutable int hfwWidth_ = -1;
	mutable int hfwHeight_ = 0;
};

} // namespace msdui
