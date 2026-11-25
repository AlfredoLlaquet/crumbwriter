#ifndef CRUMBWRITER_GUILLEMETMARKUP_H
#define CRUMBWRITER_GUILLEMETMARKUP_H

#include <QList>
#include <QString>
#include <QStringView>

namespace GuillemetMarkup {
// Source offsets of both printed signs, relative to a single physical line.
// A new opening sign abandons an unmatched earlier opener. No pair crosses
// a paragraph boundary, and every unpaired sign remains ordinary punctuation.
struct Span {
    int open;
    int close;

    bool containsCaret(int position) const { return open < position && position <= close; }
};

inline QList<Span> pairs(QStringView line)
{
    QList<Span> result;
    int open = -1;
    for (auto i = line.indexOf(u'«'); i >= 0 && i < line.size(); ++i) {
        if (line.at(i) == u'«') open = int(i);
        else if (line.at(i) == u'»' && open >= 0) {
            result.append({open, int(i)});
            open = -1;
        }
    }
    return result;
}

inline QString visibleText(QStringView line)
{
    const auto spans = pairs(line);
    if (spans.isEmpty()) return line.toString();
    QString visible;
    visible.reserve(line.size() - 2 * spans.size());
    qsizetype copied = 0;
    for (const auto &span : spans) {
        visible += line.mid(copied, span.open - copied);
        visible += line.mid(span.open + 1, span.close - span.open - 1);
        copied = span.close + 1;
    }
    visible += line.mid(copied);
    return visible;
}
}
#endif
