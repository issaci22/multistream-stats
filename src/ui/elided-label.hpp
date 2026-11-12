// SPDX-License-Identifier: GPL-2.0-or-later
//
// Single-line label that never forces the dock wider. What it shows depends on
// its role and the width it gets (1.0.1):
//
//   Role::Value  - a measured value (bitrate, frames, CPU...). Never elided:
//                  shows the full text, or its compact form when the full text
//                  does not fit ("720 / 25142 (2.9%)" -> "720 (2.9%)"). Its
//                  minimum width is the compact text, so layouts must give it
//                  that much (StatGrid shrinks labels first).
//   Role::Label  - a row caption; elides first ("Frames missed due…").
//   Role::Text   - descriptive text (encoder, host, error...): full, then
//                  compact, then elided.
//
// Whenever less than the full text is shown, the full text is appended to the
// tooltip.
//
// Performance: unlike QLabel::setText (which invalidates the whole layout on
// every change), a text change only triggers a repaint, plus a geometry
// update when the text's widest-ever width grows. Values updated every second
// therefore don't re-layout the dock.
//
// The read-only msd* properties exist for the isolated UI tests (the test
// harness reads them through QObject::property, without linking this code).
#pragma once

#include <QLabel>

namespace msdui {

class ElidedLabel : public QLabel {
	Q_OBJECT
	Q_PROPERTY(QString msdFullText READ fullText)
	Q_PROPERTY(QString msdShownText READ shownText)
	Q_PROPERTY(int msdRole READ roleId)
	Q_PROPERTY(int msdMode READ modeId) // 0 full, 1 compact, 2 elided

public:
	enum class Role { Label = 0, Value = 1, Text = 2 };
	enum class Mode { Full = 0, Compact = 1, Elided = 2 };

	explicit ElidedLabel(QWidget *parent = nullptr, Role role = Role::Label);

	void setRole(Role role);
	Role role() const { return role_; }

	// `compact` is an optional shorter rendering of the same value.
	void setFullText(const QString &text, const QString &compact = QString());
	const QString &fullText() const { return full_; }
	const QString &compactText() const { return compact_; }
	void setBaseToolTip(const QString &tip);
	const QString &baseToolTip() const { return baseTip_; }

	Mode mode() const;
	QString shownText() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	int roleId() const { return (int)role_; }
	int modeId() const { return (int)mode(); }
	int minimumTextWidth() const;
	void remeasure();
	void updateToolTip();

	Role role_;
	QString full_;
	QString compact_;
	QString baseTip_;
	int textWidth_ = 0;    // widest full text so far (grow-only)
	int compactWidth_ = 0; // widest compact text so far (grow-only)
};

} // namespace msdui
