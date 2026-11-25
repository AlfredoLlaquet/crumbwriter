#include "guillemetmarkup.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"

#include <QScopedValueRollback>
#include <QTextBlock>
#include <algorithm>
#include <utility>

namespace {
using GuillemetMarkup::Span;

struct Segment {
    int blockPosition, start, end;
    QList<Span> spans;
    int firstCovered = -1, lastCovered = -1;
};

struct MarkEdit {
    int position;
    QChar insert;  // Null removes one existing delimiter.
};

void removeEmphasis(const Segment &part, const Span &span, QList<MarkEdit> &edits)
{
    const int base = part.blockPosition;
    // A selection may include either or both signs. When only one sign was
    // selected, remove the pair without changing any of its writing.
    if (part.end <= span.open + 1 || part.start >= span.close) {
        edits.append(MarkEdit{base + span.open, {}});
        edits.append(MarkEdit{base + span.close, {}});
        return;
    }
    // Unselected text on either side of the selection keeps its italics.
    if (part.start <= span.open + 1) edits.append(MarkEdit{base + span.open, {}});
    else edits.append(MarkEdit{base + part.start, u'»'});
    if (part.end >= span.close) edits.append(MarkEdit{base + span.close, {}});
    else edits.append(MarkEdit{base + part.end, u'«'});
}

void addEmphasis(const Segment &part, QList<MarkEdit> &edits)
{
    if (part.firstCovered >= 0) return;  // Another selected paragraph needs wrapping.
    int start = part.start, end = part.end;
    bool keepOpening = false, keepClosing = false;
    // Merge intersecting pairs without introducing nested or dangling signs.
    for (const auto &span : part.spans) {
        if (span.close < start) continue;
        if (span.open >= end) break;
        if (span.open <= start) { start = span.open; keepOpening = true; }
        else edits.append(MarkEdit{part.blockPosition + span.open, {}});
        if (span.close + 1 >= end) { end = span.close + 1; keepClosing = true; }
        else edits.append(MarkEdit{part.blockPosition + span.close, {}});
    }
    if (!keepOpening) edits.append(MarkEdit{part.blockPosition + start, u'«'});
    if (!keepClosing) edits.append(MarkEdit{part.blockPosition + end, u'»'});
}
}

void ManuscriptEditor::toggleGuillemets()
{
    if (isReadOnly() || !model().isGeneric()) return;
    finishTyping();
    auto caret = textCursor();
    if (!caret.hasSelection()) {
        const int offset = caret.positionInBlock();
        const QString line = virtualRows() ? QString() : caret.block().text();
        const auto before = QStringView(line).left(qMin(offset, int(line.size())));
        const bool needsCloser = before.lastIndexOf(u'«') > before.lastIndexOf(u'»');
        if (needsCloser && offset < line.size() && line.at(offset) == u'»') {
            caret.movePosition(QTextCursor::NextCharacter);
            setTextCursor(caret);
        } else {
            insertLiteral(needsCloser ? QStringLiteral("»") : QStringLiteral("«"));
        }
        return;
    }

    auto left = at(caret.selectionStart());
    auto right = at(caret.selectionEnd());
    right.setKeepPositionOnInsert(true);
    const bool backward = caret.position() < caret.anchor();
    QList<Segment> parts;
    bool allEmphasized = true;
    for (auto block = left.block(); block.isValid() && block.position() < right.position(); block = block.next()) {
        const int base = block.position();
        const int start = qMax(0, left.position() - base);
        const int end = qMin(block.length() - 1, right.position() - base);
        if (start >= end) continue;
        Segment part{base, start, end, GuillemetMarkup::pairs(block.text())};
        for (int i = 0; i < part.spans.size(); ++i) {
            const auto &span = part.spans.at(i);
            if (span.open > start) break;
            if (span.close < start) continue;
            int last = i;
            // Adjacent pairs already emphasize a continuous passage.
            while (part.spans.at(last).close + 1 < end && last + 1 < part.spans.size() &&
                   part.spans.at(last).close + 1 == part.spans.at(last + 1).open) ++last;
            if (end <= part.spans.at(last).close + 1) {
                part.firstCovered = i;
                part.lastCovered = last;
            }
            break;
        }
        if (part.firstCovered < 0) allEmphasized = false;
        parts.append(std::move(part));
    }
    QList<MarkEdit> edits;
    for (const auto &part : std::as_const(parts)) {
        if (allEmphasized) {
            for (int i = part.firstCovered; i <= part.lastCovered; ++i)
                removeEmphasis(part, part.spans.at(i), edits);
        } else {
            addEmphasis(part, edits);
        }
    }
    if (edits.isEmpty()) return;
    std::sort(edits.begin(), edits.end(), [](const MarkEdit &a, const MarkEdit &b) {
        return a.position > b.position;
    });
    QScopedValueRollback<bool> guard(editing_, true);
    clearAlignment();
    caret.beginEditBlock();
    for (const auto &edit : std::as_const(edits)) {
        auto point = at(edit.position);
        if (edit.insert.isNull()) point.deleteChar();
        else ManuscriptText::insert(point, QString(edit.insert));
    }
    caret.endEditBlock();
    caret.setPosition(backward ? right.position() : left.position());
    caret.setPosition(backward ? left.position() : right.position(), QTextCursor::KeepAnchor);
    setTextCursor(caret);
    trackSeparator(caret);
}
