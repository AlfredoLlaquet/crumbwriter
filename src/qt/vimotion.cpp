#include "vimotion.h"
#include "manuscripttext.h"

#include <QAbstractTextDocumentLayout>
#include <QObject>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <cstdlib>

namespace {
enum class Token { Space, Word, Punctuation };

Token tokenAt(const QTextCursor &cursor, bool bigWord)
{
    const auto *doc = cursor.document();
    const QChar ch = doc->characterAt(cursor.position());
    if (cursor.atEnd() || ch.isSpace()) return Token::Space;
    char32_t cp = ch.unicode();
    if (ch.isHighSurrogate()) {
        const QChar next = doc->characterAt(cursor.position() + 1);
        if (next.isLowSurrogate()) cp = QChar::surrogateToUcs4(ch, next);
    }
    if (bigWord || cp == '_' || QChar::isLetterOrNumber(cp) || QChar::isMark(cp))
        return Token::Word;
    return Token::Punctuation;
}

bool stepCharacter(QTextCursor &cursor, bool back)
{
    const auto before = cursor.position();
    if (ManuscriptText::crossHiddenMarker(cursor, back, QTextCursor::MoveAnchor)) return cursor.position() != before;
    return cursor.movePosition(back ? QTextCursor::PreviousCharacter : QTextCursor::NextCharacter);
}

int relativeLine(QTextBlock block, int count)
{
    const bool back = count < 0;
    for (int i = 0; block.isValid() && i < std::abs(count); ++i)
        block = ManuscriptText::adjacentVisibleBlock(block, back);
    return block.isValid() ? block.blockNumber() : -1;
}

bool wordStep(QTextCursor &cursor, QChar motion)
{
    const bool big = motion.isUpper();
    if (motion.toLower() == u'b') {
        do {
            if (!stepCharacter(cursor, true)) return false;
        } while (!cursor.atStart() && tokenAt(cursor, big) == Token::Space);
        const auto kind = tokenAt(cursor, big);
        auto previous = cursor;
        while (stepCharacter(previous, true) && tokenAt(previous, big) == kind)
            cursor = previous;
        return true;
    }
    const int start = cursor.position();
    if (cursor.atEnd()) return false;
    auto skip = [&](Token kind) {
        while (!cursor.atEnd() && tokenAt(cursor, big) == kind)
            if (!stepCharacter(cursor, false)) break;
    };
    if (motion.toLower() == u'w') {
        skip(tokenAt(cursor, big));
        skip(Token::Space);
    } else {
        skip(Token::Space);
        skip(tokenAt(cursor, big)); // Exclusive Qt endpoint includes the last letter.
    }
    return cursor.position() != start;
}

int lastLine(const QTextDocument *doc)
{
    auto last = doc->lastBlock();
    if (last.blockNumber() > 0 && (!last.isVisible() || last.length() == 1))
        last = ManuscriptText::adjacentVisibleBlock(last, true);
    return last.blockNumber();
}

QTextLayout *layoutFor(const QTextBlock &block)
{
    block.document()->documentLayout()->blockBoundingRect(block);
    return block.layout();
}

std::optional<int> physicalLine(const QTextCursor &cursor, qint64 number, const QString &motion)
{
    if (number < 0 || number > lastLine(cursor.document())) return {};
    const auto target = cursor.document()->findBlockByNumber(int(number));
    if (motion == "$") return target.position() + target.length() - 1;
    if (motion == "0") return target.position();
    int column = 0;
    if (motion == "j" || motion == "k") {
        // Match the character column, including combined accents and emoji.
        const auto *source = layoutFor(cursor.block());
        const auto *dest = layoutFor(target);
        int from = 0;
        while (from < cursor.positionInBlock() && column < target.length() - 1) {
            from = source->nextCursorPosition(from);
            column = dest->nextCursorPosition(column);
        }
    } else {
        const auto text = ManuscriptText::blockText(target);
        while (column < text.size() && text[column].isSpace()) ++column;
    }
    return target.position() + column;
}

std::optional<int> screenRow(const QTextCursor &cursor, int distance)
{
    auto block = cursor.block();
    auto *layout = layoutFor(block);
    const auto first = layout->lineForTextPosition(cursor.positionInBlock());
    if (!first.isValid()) return {};
    const qreal x = first.cursorToX(cursor.positionInBlock());
    qint64 row = qint64(first.lineNumber()) + distance;
    const int last = lastLine(cursor.document());
    while (row < 0 || row >= layout->lineCount()) {
        if (row >= 0) row -= layout->lineCount();
        block = ManuscriptText::adjacentVisibleBlock(block, distance < 0);
        if (!block.isValid() || block.blockNumber() > last) return {};
        layout = layoutFor(block);
        if (distance < 0) row += layout->lineCount();
    }
    const auto line = layout->lineAt(int(row));
    int column = line.xToCursor(x);
    // At a soft wrap, the insertion point after the row belongs to the next row.
    if (row + 1 < layout->lineCount() && column == line.textStart() + line.textLength())
        column = layout->previousCursorPosition(column);
    return block.position() + column;
}

// The common case reads only the paragraphs visited. A phrase pasted with an
// actual newline requires the exact manuscript snapshot, including separators.
std::optional<int> literal(const ViMotion &motion, QTextCursor cursor)
{
    const QChar op = motion.name.front();
    const bool back = op.isUpper();
    const bool till = op.toLower() == u't';
    const auto cs = op.toLower() == u'q' ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (motion.skipLastCharacter)
        stepCharacter(cursor, back);
    const bool multiline = motion.target.contains(u'\n');
    auto block = cursor.block();
    ManuscriptText::VisibleText offsets;
    const QString visible = multiline ? offsets.reset(cursor.document(), true) : QString();
    qsizetype edge = multiline ? offsets.visiblePosition(cursor.position()) : cursor.positionInBlock();
    int remaining = motion.count;
    while (block.isValid()) {
        const auto text = multiline ? visible
                                    : ManuscriptText::blockText(block);
        if (back && edge < 0) edge = text.size();
        while (back ? edge >= motion.target.size() : edge < text.size()) {
            const qsizetype hit = back ? text.lastIndexOf(motion.target, edge - motion.target.size(), cs)
                                       : text.indexOf(motion.target, edge, cs);
            if (hit < 0) break;
            const qsizetype after = hit + motion.target.size();
            if (--remaining == 0) {
                const bool useEnd = back ? till : !till;
                const int pos = int(useEnd ? after : hit);
                return multiline ? offsets.sourcePosition(pos) : block.position() + pos;
            }
            edge = back ? hit : after;
        }
        if (multiline) break;
        block = ManuscriptText::adjacentVisibleBlock(block, back);
        edge = back ? -1 : 0;
    }
    return {};
}
}

std::optional<ViMotion> ViMotion::parse(const QString &command, QString &hint)
{
    if (command.isEmpty()) {
        hint = QObject::tr("Type a movement. Esc cancels.");
        return {};
    }
    ViMotion motion;
    const QChar op = command.front();
    motion.name = op;
    qsizetype pos = 1;
    if (op == u'g') {
        if (pos == command.size() || !QStringLiteral("gjk").contains(command[pos])) {
            hint = QObject::tr("After g, type g, j, or k.");
            return {};
        }
        motion.name += command[pos++];
    } else if (QStringLiteral("fFtT").contains(op)) {
        if (pos == command.size()) {
            hint = QObject::tr("Type a character.");
            return {};
        }
        QTextBoundaryFinder chars(QTextBoundaryFinder::Grapheme, command.constData() + pos, command.size() - pos);
        const auto length = chars.toNextBoundary();
        motion.target = command.mid(pos, length);
        pos += length;
    } else if (op.toLower() == u'q') {
        while (pos < command.size() && command[pos] != u'*') {
            if (command[pos] == u'\\' && pos + 1 < command.size() &&
                (command[pos + 1] == u'*' || command[pos + 1] == u'\\')) ++pos;
            motion.target += command[pos++];
        }
        if (motion.target.isEmpty()) {
            hint = QObject::tr("Type a phrase. Use \\* for a literal star.");
            return {};
        }
        if (pos == command.size()) return motion;
        if (++pos == command.size()) {
            hint = QObject::tr("Type an occurrence number after *.");
            return {};
        }
    } else if (!QStringLiteral("hlwWbBeEjk0^$G;,").contains(op)) {
        hint = QObject::tr("Unknown movement. Backspace to edit, or Esc.");
        return {};
    }

    if (pos < command.size()) {
        const QStringView suffix = QStringView(command).mid(pos);
        bool ok = false;
        for (const QChar digit : suffix) {
            if (digit < u'0' || digit > u'9') {
                hint = QObject::tr("The count must be a positive number.");
                return {};
            }
        }
        motion.count = suffix.toInt(&ok);
        if (!ok || motion.count < 1) {
            hint = QObject::tr("Use a positive count within range.");
            return {};
        }
        motion.explicitCount = true;
    }
    return motion;
}

std::optional<int> ViMotion::destination(QTextCursor cursor) const
{
    cursor.clearSelection();
    if (!target.isEmpty()) {
        const auto end = literal(*this, cursor);
        if (!end) return {};
        const auto block = cursor.document()->findBlock(*end);
        const auto *layout = layoutFor(block);
        const int column = *end - block.position();
        if (layout->isValidCursorPosition(column)) return end;
        // A literal can match just the base letter of a combined grapheme.
        const bool after = name == "q" || name == "f" || name == "T";
        return block.position() + (after ? layout->nextCursorPosition(column)
                                         : layout->previousCursorPosition(column));
    }
    if (name == "j" || name == "k") return physicalLine(cursor, relativeLine(cursor.block(), name == "k" ? -count : count), name);
    if (name == "gj" || name == "gk") return screenRow(cursor, name == "gk" ? -count : count);
    if (name == "gg" || name == "G") {
        const int line = explicitCount ? relativeLine(cursor.document()->begin(), count - 1) : name == "gg" ? 0 : lastLine(cursor.document());
        return physicalLine(cursor, line, name);
    }
    if (name == "$" || name == "0" || name == "^")
        return physicalLine(cursor, relativeLine(cursor.block(), name == "$" ? count - 1 : 0), name);
    if (name == "h" || name == "l") {
        for (int i = 0; i < count; ++i)
            if (!stepCharacter(cursor, name == "h")) return {};
    } else {
        for (int i = 0; i < count; ++i)
            if (!wordStep(cursor, name.front())) return {};
    }
    return cursor.position();
}
