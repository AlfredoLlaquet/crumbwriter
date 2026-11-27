#include "manuscripttext.h"
#include "genericparagraph.h"
#include <algorithm>

#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QTextFormat>

namespace {
constexpr int originalCodeUnit = QTextFormat::UserProperty + 1;
constexpr auto hasLiterals = "crumbwriterLiteralCharacters";

bool reserved(QChar ch)
{
    return ch == QChar::ParagraphSeparator || ch == QChar(0xfdd0) || ch == QChar(0xfdd1);
}

qsizetype nextReserved(const QString &text, qsizetype from = 0)
{
    for (qsizetype i = from; i < text.size(); ++i)
        if (reserved(text.at(i)))
            return i;
    return text.size();
}

QString restore(const QTextDocument *doc, int start, QString text)
{
    if (!doc->property(hasLiterals).toBool())
        return text;
    // A cursor at the end of an encoded character sees that character's own
    // format. Native Qt undo and range edits track these formats automatically.
    QTextCursor cursor(doc->begin());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != QChar::LineSeparator && text.at(i) != QChar::ReplacementCharacter)
            continue;
        cursor.setPosition(start + int(i) + 1);
        const auto original = cursor.charFormat().intProperty(originalCodeUnit);
        if (original)
            text[i] = QChar(static_cast<ushort>(original));
    }
    return text;
}
}

void ManuscriptText::load(QTextDocument *doc, const QString &text)
{
    doc->setProperty(hasLiterals, false);
    if (nextReserved(text) == text.size()) {
        doc->setPlainText(text);
        return;
    }
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    insert(cursor, text);
    cursor.endEditBlock();
    doc->setUndoRedoEnabled(undo);
}

void ManuscriptText::insert(QTextCursor &cursor, const QString &text)
{
    QTextDocument *doc = cursor.document();
    const qsizetype first = nextReserved(text);
    if (first == text.size() && !doc->property(hasLiterals).toBool()) {
        cursor.insertText(text);
        return;
    }
    QTextCharFormat normal = cursor.charFormat();
    normal.clearProperty(originalCodeUnit);
    if (first == text.size()) {
        cursor.insertText(text, normal);
        return;
    }
    doc->setProperty(hasLiterals, true);
    qsizetype pos = 0;
    for (qsizetype i = first; i < text.size(); i = nextReserved(text, pos)) {
        if (i > pos)
            cursor.insertText(text.mid(pos, i-pos), normal);
        QTextCharFormat literal = normal;
        const auto value = text.at(i);
        literal.setProperty(originalCodeUnit, int(value.unicode()));
        const QChar visible = value == QChar::ParagraphSeparator
                ? QChar::LineSeparator : QChar::ReplacementCharacter;
        cursor.insertText(QString(visible), literal);
        pos = i + 1;
    }
    if (pos < text.size())
        cursor.insertText(text.mid(pos), normal);
}

QString ManuscriptText::documentText(const QTextDocument *doc)
{
    // Qt's toPlainText() also replaces NBSP. Convert only physical line ends.
    QString text = doc->toRawText();
    text.replace(QChar::ParagraphSeparator, u'\n');
    return restore(doc, 0, text);
}

QString ManuscriptText::blockText(const QTextBlock &block)
{
    if (!block.isValid())
        return {};
    return restore(block.document(), block.position(), block.text());
}

QString ManuscriptText::selectedText(const QTextCursor &cursor)
{
    const auto *doc = cursor.document();
    const auto first = doc->findBlock(cursor.selectionStart());
    const auto last = doc->findBlock(cursor.selectionEnd());
    if (first == last) return first.isVisible() ? restore(doc, cursor.selectionStart(), cursor.selectedText()) : QString();
    QString result;
    bool seen = false;
    for (auto block = first; block.isValid(); block = block.next()) {
        if (block.isVisible()) {
            if (seen) result += u'\n';
            const int begin = qMax(0, cursor.selectionStart() - block.position());
            const int end = qMin(block.length() - 1, cursor.selectionEnd() - block.position());
            result += restore(doc, block.position() + begin, block.text().mid(begin, end - begin));
            seen = true;
        }
        if (block == last) break;
    }
    return result;
}

QString ManuscriptText::VisibleText::reset(const QTextDocument *doc, bool foldMarkers)
{
    folds_.clear();
    QString raw = documentText(doc);
    if (!foldMarkers) return raw;
    QString text;
    int copied = 0;
    for (auto block = doc->begin(); block.isValid(); block = block.next()) {
        if (block.isVisible()) continue;
        // The hidden interval includes the newline before the marker. A
        // following visible paragraph contributes its usual single newline.
        const int start = block.position() - 1;
        text += QStringView(raw).mid(copied, start - copied);
        folds_.append({start, int(text.size()), block.length()});
        copied = start + block.length();
    }
    if (folds_.isEmpty()) return raw;
    text += QStringView(raw).mid(copied);
    return text;
}

int ManuscriptText::VisibleText::sourcePosition(int visible) const
{
    auto it = std::lower_bound(folds_.cbegin(), folds_.cend(), visible,
                               [](Fold f, int pos) { return f.visible < pos; });
    if (it == folds_.cbegin()) return visible;
    --it;
    return visible + it->source + it->length - it->visible;
}

int ManuscriptText::VisibleText::visiblePosition(int source) const
{
    auto it = std::lower_bound(folds_.cbegin(), folds_.cend(), source,
                               [](Fold f, int pos) { return f.source < pos; });
    if (it == folds_.cbegin()) return source;
    --it;
    return it->visible + qMax(0, source - it->source - it->length);
}

QTextBlock ManuscriptText::adjacentVisibleBlock(QTextBlock block, bool backward)
{
    do { block = backward ? block.previous() : block.next(); } while (block.isValid() && !block.isVisible());
    return block;
}

bool ManuscriptText::crossHiddenMarker(QTextCursor &cursor, bool backward, QTextCursor::MoveMode mode)
{
    if (backward ? !cursor.atBlockStart() : !cursor.atBlockEnd()) return false;
    const auto adjacent = backward ? cursor.block().previous() : cursor.block().next();
    if (!adjacent.isValid() || adjacent.isVisible()) return false;
    if (backward) cursor.setPosition(adjacent.position() - 1, mode);
    else if (adjacent.next().isValid()) cursor.setPosition(adjacent.next().position(), mode);
    return true;
}

void ManuscriptText::replaceVisibleRange(QTextCursor &cursor, const QString &text)
{
    const auto *doc = cursor.document();
    const auto first = doc->findBlock(cursor.selectionStart());
    const auto last = doc->findBlock(cursor.selectionEnd());
    auto marker = [](const QTextBlock &paragraph) {
        const auto next = paragraph.next();
        return next.isValid() && !next.isVisible() ? next.text() : QString();
    };
    const QString firstMark = marker(first), lastMark = marker(last);
    if (firstMark.isEmpty() && lastMark.isEmpty()) {
        insert(cursor, text);
        return;
    }

    const int start = cursor.selectionStart() - first.position(), end = cursor.selectionEnd() - last.position();
    const auto prefix = first.text().left(start), suffix = last.text().mid(end);
    const auto newline = text.indexOf(u'\n');
    QString leading, trailing;
    if (first == last) {
        if (newline < 0 || (prefix.isEmpty() && !suffix.isEmpty())) trailing = firstMark;
        else leading = firstMark;
    } else {
        if (!prefix.isEmpty()) leading = firstMark;
        if (!suffix.isEmpty()) trailing = lastMark;
        if (newline < 0 && !leading.isEmpty()) { trailing = leading; leading.clear(); }
    }
    const auto lastLine = newline < 0 ? prefix + text + suffix : text.mid(text.lastIndexOf(u'\n') + 1) + suffix;
    if (!trailing.isEmpty() && !GenericParagraph::canHaveMarker(lastLine)) trailing.clear();
    QString inserted = text;
    if (!leading.isEmpty() && newline >= 0 && GenericParagraph::canHaveMarker(prefix + text.left(newline)))
        inserted.insert(newline, u'\n' + leading);
    // Relocate only the marker. All surviving text and its live cursors retain
    // their identity, and the caller's edit block makes this one undo step.
    if (!lastMark.isEmpty() && lastMark != trailing) {
        QTextCursor remove(last);
        remove.movePosition(QTextCursor::EndOfBlock);
        remove.setPosition(last.next().position() + last.next().length() - 1, QTextCursor::KeepAnchor);
        remove.removeSelectedText();
    }
    insert(cursor, inserted);
    if (!trailing.isEmpty() && lastMark != trailing) {
        const bool keep = cursor.keepPositionOnInsert();
        cursor.setKeepPositionOnInsert(true);
        QTextCursor append(cursor.block());
        append.movePosition(QTextCursor::EndOfBlock);
        insert(append, u'\n' + trailing);
        cursor.setKeepPositionOnInsert(keep);
    }
}
