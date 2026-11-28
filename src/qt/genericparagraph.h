#ifndef CRUMBWRITER_GENERICPARAGRAPH_H
#define CRUMBWRITER_GENERICPARAGRAPH_H

#include "guillemetmarkup.h"
#include <QStringView>
#include <QTextBoundaryFinder>

namespace GenericParagraph {
inline constexpr qreal mainTitleScale = 1.7;
inline constexpr qreal titleScales[] = {1.48, 1.30, 1.16, 1.0};

// A marker starts in column one. Only ordinary horizontal separators
// after its final mark are ignored, just as they are by the file writer.
inline int markerLevel(QStringView line)
{
    if (line.isEmpty()) return 0;
    const QChar mark = line.front();
    int level;
    switch (mark.unicode()) {
    case '=': level = 1; break;
    case '-': level = 2; break;
    case '~': level = 3; break;
    case '.': level = 4; break;
    default: return 0;
    }
    qsizetype pos = 1;
    while (pos < line.size() && line[pos] == mark) ++pos;
    while (pos < line.size() && (line[pos] == u' ' || line[pos] == u'\t')) ++pos;
    return pos == line.size() ? level : 0;
}

// A first-line manuscript title uses the same '=' marker as a level-one heading.
inline bool markerAllowed(int headingLine, int level)
{
    return level > 0 && (headingLine > 0 || (headingLine == 0 && level == 1));
}

inline bool canHaveMarker(QStringView paragraph)
{
    return !paragraph.trimmed().isEmpty() && !markerLevel(paragraph);
}

inline qsizetype markerLength(QStringView title, bool literalGuillemets = false)
{
    QString visible;
    if (!literalGuillemets && title.contains(u'«')) {
        visible = GuillemetMarkup::visibleText(title);
        title = visible;
    }
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, title.data(), title.size());
    qsizetype count = 0;
    while (graphemes.toNextBoundary() >= 0) ++count;
    return qMax<qsizetype>(1, count);
}
}
#endif
