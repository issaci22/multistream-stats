// SPDX-License-Identifier: GPL-2.0-or-later
//
// Small expand/collapse triangle drawn in the theme's text color (no boxed
// button chrome), sized from the font so it scales with DPI/font settings.
// Checked = expanded (pointing down).
#pragma once

#include <QAbstractButton>

namespace msdui {

class DisclosureArrow : public QAbstractButton {
public:
	explicit DisclosureArrow(QWidget *parent = nullptr);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent *event) override;
};

} // namespace msdui
