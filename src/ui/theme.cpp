// SPDX-License-Identifier: GPL-2.0-or-later
#include "theme.hpp"

#include <obs-module.h>

#include <QVariant>
#include <QWidget>

namespace msdui {

void setClasses(QWidget *widget, const QString &classes)
{
	if (!widget)
		return;
	if (widget->property("class").toString() == classes)
		return;
	widget->setProperty("class", classes);
	// Force Qt to re-evaluate the style sheet for the new property value
	// (same technique as OBS's setClasses()).
	const QString qss = widget->styleSheet();
	widget->setStyleSheet("/* */");
	widget->setStyleSheet(qss);
}

QString healthClass(msd::Health health)
{
	switch (health) {
	case msd::Health::Good:
		return QStringLiteral("text-success");
	case msd::Health::Warning:
		return QStringLiteral("text-warning");
	case msd::Health::Bad:
		return QStringLiteral("text-danger");
	case msd::Health::Inactive:
	default:
		return QStringLiteral("text-muted");
	}
}

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString naText()
{
	return T("NA");
}

QString naTooltip(msd::Unavailable reason)
{
	switch (reason) {
	case msd::Unavailable::NotSupported:
		return T("NA.NotSupported");
	case msd::Unavailable::NotActive:
		return T("NA.NotActive");
	case msd::Unavailable::NotMeasured:
		return T("NA.NotMeasured");
	case msd::Unavailable::Unknown:
		return T("NA.Unknown");
	case msd::Unavailable::None:
	default:
		return QString();
	}
}

} // namespace msdui
