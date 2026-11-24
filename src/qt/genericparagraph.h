#ifndef CRUMBWRITER_GENERICPARAGRAPH_H
#define CRUMBWRITER_GENERICPARAGRAPH_H

#include <QStringView>
#include <QTextBoundaryFinder>
#include <algorithm>

namespace GenericParagraph {
inline bool onlyHyphens(QStringView line)
{
    return !line.isEmpty() && line.front() == u'-' &&
           std::all_of(line.cbegin() + 1, line.cend(), [](QChar ch) { return ch == u'-'; });
}

inline qsizetype markerLength(QStringView title)
{
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, title.data(), title.size());
    qsizetype count = 0;
    while (graphemes.toNextBoundary() >= 0) ++count;
    return count;
}
}
#endif
