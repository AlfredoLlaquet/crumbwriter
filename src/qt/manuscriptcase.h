#pragma once

#include <QString>
#include <glib.h>

namespace ManuscriptCase {
// Qt's per-character folding cannot expand ß into ss. GLib provides full
// Unicode folding; ordinary ASCII writing needs only the inexpensive path.
inline QString fold(const QString &text)
{
    for (const auto c : text) {
        if (c.unicode() < 128) continue;
        const auto utf8 = text.toUtf8();
        char *folded = g_utf8_casefold(utf8.constData(), utf8.size());
        const auto result = QString::fromUtf8(folded).normalized(QString::NormalizationForm_C);
        g_free(folded);
        return result;
    }
    return text.toLower();
}
}
