#ifndef CRUMBWRITER_MANUSCRIPTSPACING_H
#define CRUMBWRITER_MANUSCRIPTSPACING_H

#include <QString>
#include <QStringView>

namespace ManuscriptSpacing {
inline bool horizontal(QChar ch) { return ch == u' ' || ch == u'\t'; }

inline QString normalizeInsertion(QString text, bool keepFinalSpace)
{
    // Compact in place. Newlines delimit paragraphs; NBSP and other Unicode
    // spacing characters are ordinary manuscript data.
    const qsizetype length = text.size();
    auto *data = text.data();
    qsizetype written = 0;
    bool space = false;
    for (qsizetype read = 0; read < length; ++read) {
        QChar ch = data[read];
        if (horizontal(ch)) {
            space = true;
            continue;
        }
        if (ch == u'\r') {
            if (read + 1 < length && data[read + 1] == u'\n')
                ++read;
            ch = u'\n';
        }
        if (space && ch != u'\n')
            data[written++] = u' ';
        space = false;
        data[written++] = ch;
    }
    if (space && keepFinalSpace)
        data[written++] = u' ';
    text.resize(written);
    return text;
}

// Discard only complete blank paragraphs at EOF. Opening preserves a missing
// final terminator. Publication gives each nonempty file one final terminator.
inline int trimBlankEnd(QString &text, bool terminate)
{
    qsizetype end = text.size();
    int removedBreaks = 0;
    const QStringView manuscript(text);
    while (end > 0) {
        const auto start = manuscript.left(end).lastIndexOf(u'\n') + 1;
        if (!manuscript.mid(start, end - start).trimmed().isEmpty()) break;
        if (start) ++removedBreaks;
        end = qMax<qsizetype>(0, start - 1);
    }
    const bool finalBreak = end && (terminate || end < text.size());
    text.truncate(end);
    if (finalBreak) text += u'\n';
    return qMax(0, removedBreaks - int(finalBreak));
}
}
#endif
