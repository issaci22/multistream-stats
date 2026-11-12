// SPDX-License-Identifier: GPL-2.0-or-later
//
// Helpers to put a value or an explained "N/A" into a value label.
#pragma once

#include "elided-label.hpp"
#include "theme.hpp"

#include <QVariant>

namespace msdui {

inline constexpr const char *kTipProperty = "msdTip";

// `compact` is the shorter form shown when the full text does not fit (the full
// text then appears in the tooltip). Empty = the value has no shorter form.
inline void setValue(ElidedLabel *label, const QString &text, const QString &cls = QString(),
		     const QString &extraTip = QString(), const QString &compact = QString())
{
	label->setFullText(text, compact);
	setClasses(label, cls);
	QString tip = label->property(kTipProperty).toString();
	if (!extraTip.isEmpty())
		tip = tip.isEmpty() ? extraTip : tip + QStringLiteral("\n\n") + extraTip;
	label->setBaseToolTip(tip);
}

inline void setNA(ElidedLabel *label, msd::Unavailable reason)
{
	setValue(label, naText(), QStringLiteral("text-muted"), naTooltip(reason));
}

// Shows exactly what `src` shows (text, compact form, color, tooltip). Used for
// the output table's cells, which repeat values of the detail grid.
inline void mirrorValue(ElidedLabel *dst, const ElidedLabel *src)
{
	dst->setFullText(src->fullText(), src->compactText());
	setClasses(dst, src->property("class").toString());
	dst->setBaseToolTip(src->baseToolTip());
}

} // namespace msdui
