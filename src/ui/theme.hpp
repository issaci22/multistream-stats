// SPDX-License-Identifier: GPL-2.0-or-later
//
// Theme integration. Colors are never hard-coded: widgets get the semantic
// classes defined by every OBS 30+ theme (text-success / text-warning /
// text-danger / text-muted), exactly like OBS's own Stats dock does via its
// setClasses() helper (shared/qt/wrappers, not exported to plugins, so it is
// re-implemented here with identical behaviour).
#pragma once

#include <core/metric.hpp>
#include <core/status-model.hpp>

#include <QString>

class QWidget;

namespace msdui {

void setClasses(QWidget *widget, const QString &classes);
QString healthClass(msd::Health health);

// Localised text from data/locale/*.ini
QString T(const char *key);

QString naText();
QString naTooltip(msd::Unavailable reason);

} // namespace msdui
