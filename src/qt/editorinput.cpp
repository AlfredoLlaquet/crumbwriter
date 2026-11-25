#include "manuscripteditor.h"
#include "manuscriptlinks.h"
#include "textkeys.h"
#include "committedpunctuation.h"
#include "genericlist.h"
#include "manuscripttext.h"
#include "manuscriptspacing.h"
#include "viselection.h"
#include "wordselection.h"

#include <algorithm>
#include <climits>

#include <QtCore/QMimeData>
#include <QtCore/QScopedValueRollback>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtWidgets/QMenu>

namespace {
using ManuscriptSpacing::horizontal;
bool newline(QChar ch) { return ch == u'\n' || ch == QChar::ParagraphSeparator; }
}

QString ManuscriptEditor::normalizeInsertion(QString text, bool keepFinalSpace)
{
    return ManuscriptSpacing::normalizeInsertion(std::move(text), keepFinalSpace);
}

void ManuscriptEditor::trackSeparator(const QTextCursor &cursor)
{
    model_->confirmMarkerOutside(cursor.blockNumber());
    pendingSpace_ = {};
    if (!cursor.hasSelection() && cursor.atBlockEnd() && cursor.positionInBlock() > 0 &&
        document()->characterAt(cursor.position() - 1) == u' ') {
        pendingSpace_ = cursor;
        pendingSpace_.setKeepPositionOnInsert(true);
    }
}

void ManuscriptEditor::trimPending(bool joinPrevious)
{
    if (pendingSpace_.isNull())
        return;
    auto p = pendingSpace_;
    pendingSpace_ = {};
    if (!p.atBlockEnd() || !p.positionInBlock() || document()->characterAt(p.position()-1) != u' ')
        return;
    QScopedValueRollback<bool> guard(editing_, true);
    if (joinPrevious)
        p.joinPreviousEditBlock();
    p.deletePreviousChar();
    if (joinPrevious)
        p.endEditBlock();
}

bool ManuscriptEditor::replaceAt(QTextCursor &cursor, const QString &input, Input kind)
{
    int first = cursor.selectionStart();
    int end = cursor.selectionEnd();
    const QChar right = document()->characterAt(end);
    const bool atEnd = end == document()->characterCount() - 1 || newline(right);
    QString text = normalizeInsertion(input, kind == Input::Typed || !atEnd);
    const bool leftSpace = first > 0 && horizontal(document()->characterAt(first - 1));

    if (leftSpace) {
        if (text.startsWith(u' '))
            text.remove(0, 1);
        // Typing and erasing can expose a separator at the active caret.
        // trackSeparator keeps it for the next word and storage trims its copy.
        const bool literal = kind == Input::Paste || kind == Input::Replacement;
        if (text.startsWith(u'\n') || (literal && text.isEmpty() && atEnd))
            --first;
    }
    // At most one horizontal separator exists on either side of an already
    // normalized selection. Include just the overlapping byte-sized space.
    if (horizontal(right) && (text.endsWith(u' ') || (text.isEmpty() && leftSpace)))
        ++end;
    cursor.setPosition(first);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    if (text.isEmpty()) {
        if (!cursor.hasSelection()) return false;
        cursor.removeSelectedText();
    } else {
        ManuscriptText::insert(cursor, text);
    }
    if (kind == Input::Typed && smart_)
        punctuation_->feed(document(), first, text, model_->isGeneric());
    if ((kind == Input::Typed && !text.endsWith(u'\n')) || kind == Input::Erase)
        model_->deferTypedMarker(cursor.block());
    return true;
}

void ManuscriptEditor::recordEditForHash(QTextCursor &cursor, const QString &input)
{
    // Pasted hashes are literal. Only an actual typed hash can continue a
    // number. A multi-character IM commit ending in a hash follows the same rule.
    if (!model_->isGeneric() && input.endsWith(u'#') && cursor.positionInBlock() > 0) {
        const auto digits = model_->completeGenre(cursor.blockNumber(), cursor.positionInBlock()-1);
        if (!digits.isEmpty())
            ManuscriptText::insert(cursor, digits);
    }
}

void ManuscriptEditor::beginInputBlock(QTextCursor &cursor, EditRun run)
{
    // Close each key's transaction before returning to Qt. Observers and live
    // saving see every edit. Only the undo boundary joins the preceding key.
    if (run != EditRun::None && run == editRun_ && !textCursor().hasSelection())
        cursor.joinPreviousEditBlock();
    else
        cursor.beginEditBlock();
}

void ManuscriptEditor::setCopyDeletedText(bool enabled)
{
    if (copyDeleted_ == enabled) return;
    finishTyping();
    copyDeleted_ = enabled;
}

QString ManuscriptEditor::removedSelection(const QTextCursor &cursor) const
{
    return copyDeleted_ && cursor.hasSelection() ? ManuscriptText::selectedText(cursor) : QString();
}

void ManuscriptEditor::copyRemovedText(const QString &text)
{
    if (copyDeleted_ && !text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void ManuscriptEditor::insertListPrefix(QTextCursor &cursor, const QString &prefix)
{
    if (prefix.isEmpty()) return;
    GenericList::insertPrefix(cursor, prefix);
    // A newly suggested "- " is still being written, just like a typed one.
    model_->deferTypedMarker(cursor.block());
}

bool ManuscriptEditor::typeListItem(QTextCursor &cursor, const QString &text)
{
    if (!model_->isGeneric() || cursor.hasSelection() || text.isEmpty()) return false;
    const bool lineBreak = text == QStringLiteral("\n");
    if (lineBreak) {
        if (GenericList::selectEmptyGeneratedPrefix(cursor)) {
            cursor.removeSelectedText();
            declinedListParagraph_ = cursor.block();
            return true;
        }
        // An item only continues into a new, empty paragraph. Ordinary Enter
        // in the middle of a sentence still splits the existing prose.
        if (!cursor.atBlockEnd()) return false;
        const QString prefix = GenericList::nextPrefix(cursor.block());
        if (prefix.isEmpty()) return false;
        replaceAt(cursor, text, Input::Typed);
        insertListPrefix(cursor, prefix);
        return true;
    }
    // First committed text in an existing blank (or materialized virtual)
    // paragraph follows its neighbor. Visiting that paragraph never edits it.
    if (!cursor.atBlockStart() || !cursor.atBlockEnd() || cursor.block() == declinedListParagraph_ ||
        text.contains(u'\n') || text.contains(u'\r')) return false;
    const QString prefix = GenericList::nextPrefix(cursor.block().previous());
    if (prefix.isEmpty()) return false;
    insertListPrefix(cursor, prefix);
    replaceAt(cursor, text, Input::Typed);
    return true;
}

void ManuscriptEditor::continueList()
{
    if (isReadOnly() || !model_->isGeneric() || textCursor().hasSelection() || rows_ > 1) return;
    auto cursor = textCursor();
    if (rows_) cursor.movePosition(QTextCursor::End);
    const bool openParagraph = rows_ || !cursor.block().text().isEmpty();
    const auto item = openParagraph ? cursor.block() : cursor.block().previous();
    const QString prefix = GenericList::nextPrefix(item, true);
    if (prefix.isEmpty()) return;

    finishTyping();
    QScopedValueRollback<bool> guard(editing_, true);
    clearVirtual();
    clearAlignment();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.beginEditBlock();
    if (openParagraph) replaceAt(cursor, QStringLiteral("\n"), Input::Typed);
    insertListPrefix(cursor, prefix);
    cursor.endEditBlock();
    setTextCursor(cursor);
    trackSeparator(cursor);
    setFocus();
    ensureCursorVisible();
}

void ManuscriptEditor::typeText(const QString &text, int replaceStart, int replaceLength)
{
    if (isReadOnly())
        return;
    QScopedValueRollback<bool> guard(editing_, true);
    clearAlignment();
    auto c = textCursor();
    const bool separate = replaceStart || replaceLength || text.contains(u'\n') || text.contains(u'\r');
    const auto run = separate ? EditRun::None : EditRun::Typing;
    beginInputBlock(c, run);
    if (rows_) {
        const int count = rows_;
        clearVirtual();
        c.movePosition(QTextCursor::End);
        ManuscriptText::insert(c, QString(count, u'\n'));
    }
    QString selectionRemovedByIme;
    const int selectionStart = c.selectionStart();
    if (replaceStart || replaceLength) {
        if (c.hasSelection()) {
            selectionRemovedByIme = removedSelection(c);
            c.removeSelectedText();
        }
        const int pos = clampPosition(c.position() + replaceStart);
        c.setPosition(pos);
        c.setPosition(clampPosition(pos + replaceLength), QTextCursor::KeepAnchor);
        punctuation_->reset();
    } else if (c.hasSelection()) {
        punctuation_->reset();
    } else if (overwriteMode() && !text.contains(u'\n')) {
        for (qsizetype n = 0; n < text.size() && !c.atBlockEnd(); ++n) {
            if (!text.at(n).isLowSurrogate())
                c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        }
    }
    // An input method may remove both the selection and a surrounding range.
    // Reconstruct the original reading order before either is published.
    QString removed = removedSelection(c);
    if (!selectionRemovedByIme.isEmpty()) {
        const int split = qBound(0, selectionStart - c.selectionStart(), int(removed.size()));
        removed.insert(split, selectionRemovedByIme);
    }
    // A discarded duplicate space creates no undo command to join later.
    const bool continued = !replaceStart && !replaceLength && typeListItem(c, text);
    const bool changed = continued || replaceAt(c, text, Input::Typed);
    if (changed) editRun_ = run;
    else if (editRun_ != run) editRun_ = EditRun::None;
    recordEditForHash(c, text);
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
    copyRemovedText(removed);
    if (changed && !text.isEmpty() && onTypedText) onTypedText();
}

void ManuscriptEditor::insertSelection(const QString &text, Input kind)
{
    if (isReadOnly())
        return;
    finishTyping();
    QScopedValueRollback<bool> guard(editing_, true);
    clearAlignment();
    auto c = textCursor();
    c.beginEditBlock();
    if (rows_) {
        const int count = rows_;
        clearVirtual();
        c.movePosition(QTextCursor::End);
        ManuscriptText::insert(c, QString(count, u'\n'));
    }
    const QString removed = removedSelection(c);
    replaceAt(c, text, kind);
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
    copyRemovedText(removed);
}

int ManuscriptEditor::replaceRangeLiteral(int start, int end, const QString &text, bool copyRemoval)
{
    // Search may use this inside an outer edit block spanning many ranges.
    if (isReadOnly())
        return start;
    auto c = at(start);
    c.setPosition(clampPosition(end), QTextCursor::KeepAnchor);
    finishTyping();
    QScopedValueRollback<bool> guard(editing_, true);
    const QString removed = copyRemoval ? removedSelection(c) : QString();
    c.beginEditBlock();
    replaceAt(c, text, Input::Replacement);
    c.endEditBlock();
    copyRemovedText(removed);
    return c.position();
}

void ManuscriptEditor::insertFromMimeData(const QMimeData *source)
{
    if (source && source->hasText())
        insertLiteral(source->text());
}

void ManuscriptEditor::insertLiteral(const QString &text)
{
    if (!text.isEmpty())
        insertSelection(text, Input::Paste);
}

QMimeData *ManuscriptEditor::createMimeDataFromSelection() const
{
    auto *mime = new QMimeData;
    mime->setText(ManuscriptText::selectedText(textCursor()));
    return mime;
}

void ManuscriptEditor::cut()
{
    if (isReadOnly() || !textCursor().hasSelection())
        return;
    if (!copyDeleted_) QGuiApplication::clipboard()->setMimeData(createMimeDataFromSelection());
    insertSelection({}, Input::Erase);
}

void ManuscriptEditor::undo() { replayEdit(false); }
void ManuscriptEditor::redo() { replayEdit(true); }

void ManuscriptEditor::removeByKey(bool backward, bool byWord)
{
    auto c = textCursor();
    if (backward && !byWord && model_->isGeneric() && GenericList::selectEmptyGeneratedPrefix(c)) {
        declinedListParagraph_ = c.block();
        eraseSelection(c, EditRun::Backspace);
        return;
    }
    if (!c.hasSelection()) {
        const auto op = backward ? (byWord ? QTextCursor::PreviousWord : QTextCursor::PreviousCharacter)
                                 : (byWord ? QTextCursor::NextWord : QTextCursor::NextCharacter);
        c.movePosition(op, QTextCursor::KeepAnchor);
    }
    const auto run = byWord || textCursor().hasSelection() ? EditRun::None
                          : backward ? EditRun::Backspace : EditRun::Delete;
    eraseSelection(c, run);
}

void ManuscriptEditor::deleteWord()
{
    if (rows_ || isReadOnly())
        return;
    auto c = textCursor();
    if (!c.hasSelection()) {
        if (c.atBlockEnd() && !c.atBlockStart()) c.movePosition(QTextCursor::PreviousCharacter);
        c = WordSelection::atCharacter(c);
    }
    eraseSelection(c);
}

void ManuscriptEditor::eraseSelection(QTextCursor c, EditRun run)
{
    if (isReadOnly() || !c.hasSelection()) {
        if (run != editRun_) editRun_ = EditRun::None;
        return;
    }
    punctuation_->reset();
    QScopedValueRollback<bool> guard(editing_, true);
    clearAlignment();
    beginInputBlock(c, run);
    const QString removed = run == EditRun::None ? removedSelection(c) : QString();
    replaceAt(c, {}, Input::Erase);
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
    copyRemovedText(removed);
    editRun_ = run;
}

void ManuscriptEditor::showPreedit(QInputMethodEvent *event)
{
    const QString &draft = event->preeditString();
    if (draft.isEmpty() || !model_->isGeneric()) {
        QPlainTextEdit::inputMethodEvent(event);
        return;
    }

    // Supply the heading or prose font across the draft, keeping the input
    // method's own candidate styling.
    QTextCharFormat writing;
    writing.setFont(inputMethodQuery(Qt::ImFont).value<QFont>());
    auto attributes = event->attributes();
    QVector<std::pair<int, int>> spans;
    const int length = int(draft.size());
    for (auto &a : attributes) {
        if (a.type != QInputMethodEvent::TextFormat) continue;
        auto format = writing;
        format.merge(a.value.value<QTextFormat>());
        a.value = QVariant::fromValue<QTextFormat>(format);
        const int first = qBound(0, a.start, length);
        const int last = int(std::clamp<qint64>(qint64(a.start) + a.length, first, length));
        if (last > first)
            spans.append({first, last});
    }
    std::sort(spans.begin(), spans.end());
    int end = 0;
    auto addGap = [&](int next) {
        if (end < next)
            attributes.append({QInputMethodEvent::TextFormat, end, next - end, writing});
    };
    for (const auto &[first, last] : spans) {
        addGap(first);
        end = qMax(end, last);
    }
    addGap(length);
    QInputMethodEvent display(draft, attributes);
    QPlainTextEdit::inputMethodEvent(&display);
    event->setAccepted(display.isAccepted());
}

void ManuscriptEditor::inputMethodEvent(QInputMethodEvent *e)
{
    if (vi_->active()) {
        vi_->inputMethod(*e);
        e->accept();
        return;
    }
    if (e->commitString().isEmpty() && !e->replacementLength()) {
        showPreedit(e);
        return;
    }
    // Let Qt own preedit display and candidate attributes. Only the committed
    // string enters the text buffer and the smart-punctuation undo action.
    if (!textCursor().block().layout()->preeditAreaText().isEmpty()) {
        QInputMethodEvent clear;
        QPlainTextEdit::inputMethodEvent(&clear);
    }
    typeText(e->commitString(), e->replacementStart(), e->replacementLength());
    if (!e->preeditString().isEmpty() || !e->attributes().isEmpty()) {
        QInputMethodEvent preedit(e->preeditString(), e->attributes());
        showPreedit(&preedit);
    }
    e->accept();
}

bool ManuscriptEditor::event(QEvent *e)
{
    if (vi_ && vi_->active()) {
        if (e->type() == QEvent::InputMethodQuery) {
            QPlainTextEdit::event(e);
            vi_->inputMethodQuery(*static_cast<QInputMethodQueryEvent *>(e));
            return true;
        }
        if (e->type() == QEvent::ShortcutOverride) {
            e->accept();
            return true;
        }
        if (e->type() == QEvent::KeyPress) {
            vi_->key(*static_cast<QKeyEvent *>(e));
            e->accept();
            return true;
        }
    }
    if (e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent*>(e);
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) &&
            (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
            navigate(key->key() == Qt::Key_Backtab || mods == Qt::ShiftModifier, true);
            return true;
        }
    }
    return QPlainTextEdit::event(e);
}

void ManuscriptEditor::keyPressEvent(QKeyEvent *e)
{
    const int key = e->key();
    const auto mods = e->modifiers() & ~Qt::KeypadModifier;
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (isComposedTextKey(*e)) { typeText(e->text()); return; }
#ifdef Q_OS_MACOS
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_Space) {
        startViSelection();
        return;
    }
    // StandardKey on macOS describes physical Command. The manuscript also
    // accepts its documented physical Control alternatives.
    if ((mods == (Qt::ControlModifier | Qt::ShiftModifier) || mods == (Qt::MetaModifier | Qt::ShiftModifier)) &&
        key == Qt::Key_Z) { redo(); return; }
    if (mods == Qt::ControlModifier || mods == Qt::MetaModifier) {
        switch (key) {
        case Qt::Key_A: selectAll(); return;
        case Qt::Key_C: copy(); return;
        case Qt::Key_V: paste(); return;
        case Qt::Key_X: cut(); return;
        case Qt::Key_Z: undo(); return;
        default: break;
        }
    }
#endif
    if (enter && mods == Qt::ControlModifier) { centerCurrentLine(); return; }
    if (enter && mods == Qt::AltModifier) { topCurrentCrumb(); return; }
    if (enter && mods == (Qt::ControlModifier | Qt::ShiftModifier)) { continueList(); return; }
    if ((mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier)) &&
        moveByControlKey(key, mods.testFlag(Qt::ShiftModifier))) return;
    if (mods == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_Q: copyCrumb(); return;
        case Qt::Key_1: copyHeader(); return;
        case Qt::Key_Y: deleteCrumb(); return;
        case Qt::Key_W: deleteWord(); return;
        case Qt::Key_K:
            if (model_->isGeneric()) { toggleGuillemets(); return; }
            break;
        case Qt::Key_N: appendCrumb(); return;
        case Qt::Key_Space: startViSelection(); return;
        default: break;
        }
    }
    if (e->matches(QKeySequence::Undo)) { undo(); return; }
    if (e->matches(QKeySequence::Redo)) { redo(); return; }
    if (e->matches(QKeySequence::Cut)) { cut(); return; }
    if (e->matches(QKeySequence::Paste)) { paste(); return; }
    if (key == Qt::Key_Escape && !mods) { clearEditorSelection(); return; }

    if (!mods && key == Qt::Key_Down && !textCursor().hasSelection() &&
        (rows_ || onLastVisualRow())) {
        if (rows_ < INT_MAX - document()->characterCount())
            setVirtualRows(rows_ + 1);
        return;
    }
    if (rows_) {
        if (!mods && (key == Qt::Key_Up || key == Qt::Key_Left || key == Qt::Key_Backspace)) {
            setVirtualRows(rows_ - 1);
            return;
        }
        if (key == Qt::Key_Delete || (!mods && (key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End)))
            return;
        if (key == Qt::Key_PageDown || key == Qt::Key_PageUp || (mods && key == Qt::Key_Backspace))
            clearVirtual();
    }
    if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) &&
        (mods == Qt::NoModifier || mods == Qt::ControlModifier)) {
        removeByKey(key == Qt::Key_Backspace, mods == Qt::ControlModifier);
        return;
    }
    if (enter && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        typeText(QStringLiteral("\n"));
        return;
    }
    if (!e->text().isEmpty() && e->text().front().unicode() >= 0x20 &&
        !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        typeText(e->text());
        return;
    }
    // Include the platform's alternative editing keys, such as Option–Delete
    // on macOS, in the same deletion and clipboard policy as the physical keys.
    if (e->matches(QKeySequence::DeleteStartOfWord) || e->matches(QKeySequence::DeleteEndOfWord)) {
        removeByKey(e->matches(QKeySequence::DeleteStartOfWord), true);
        return;
    }
    if (e->matches(QKeySequence::Backspace) || e->matches(QKeySequence::Delete)) {
        removeByKey(e->matches(QKeySequence::Backspace), false);
        return;
    }
    if (e->matches(QKeySequence::DeleteEndOfLine) || e->matches(QKeySequence::DeleteCompleteLine)) {
        auto c = textCursor();
        if (!c.hasSelection()) {
            if (e->matches(QKeySequence::DeleteCompleteLine)) {
                c.movePosition(QTextCursor::StartOfBlock);
                c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            } else if (c.atBlockEnd()) {
                c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            } else {
                c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            }
        }
        eraseSelection(c);
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

// cursorForPosition snaps to an insertion point even in the empty margin.
// A pointer gesture must hit an actual printed character to offer its link.
int ManuscriptEditor::characterUnderPointer(const QPoint &point) const
{
    const auto cursor = cursorForPosition(point);
    const QRect caret = cursorRect(cursor);
    if (point.y() < caret.top() || point.y() > caret.bottom()) return -1;
    const auto line = cursor.block().layout()->lineForTextPosition(cursor.positionInBlock());
    if (!line.isValid()) return -1;
    const qreal x = point.x() - caret.left() + line.cursorToX(cursor.positionInBlock());
    const auto bounds = line.naturalTextRect();
    if (x < bounds.left() || x >= bounds.right()) return -1;
    return cursor.block().position() + line.xToCursor(x, QTextLine::CursorOnCharacter);
}

void ManuscriptEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && !rows_) {
        const int position = characterUnderPointer(event->position().toPoint());
        if (position >= 0) {
            QPlainTextEdit::mouseDoubleClickEvent(event);
            setTextCursor(WordSelection::atCharacter(at(position)));
            return;
        }
    }
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

void ManuscriptEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const bool keyboard = event->reason() == QContextMenuEvent::Keyboard;
    const int pos = keyboard ? (virtualRows() ? -1 : textCursor().position())
                             : characterUnderPointer(event->pos());
    if (pos >= 0) {
        const auto block = document()->findBlock(pos);
        const auto link = ManuscriptLinks::at(ManuscriptText::blockText(block), pos - block.position(), keyboard);
        if (!link.isEmpty())
            menu.addAction(tr("&Open Link"), this, [this, link] { ManuscriptLinks::open(link, this); });
    }
    if (spellingMenu && pos >= 0)
        spellingMenu(&menu, pos);
    if (!menu.isEmpty())
        menu.addSeparator();
    auto *undoAction = menu.addAction(tr("Undo"), this, &ManuscriptEditor::undo);
    undoAction->setEnabled(document()->isUndoAvailable());
    auto *redoAction = menu.addAction(tr("Redo"), this, &ManuscriptEditor::redo);
    redoAction->setEnabled(document()->isRedoAvailable());
    menu.addSeparator();
    auto *cutAction = menu.addAction(tr("Cut"), this, &ManuscriptEditor::cut);
    cutAction->setEnabled(textCursor().hasSelection() && !isReadOnly());
    auto *copyAction = menu.addAction(tr("Copy"), this, &QPlainTextEdit::copy);
    copyAction->setEnabled(textCursor().hasSelection());
    auto *pasteAction = menu.addAction(tr("Paste"), this, &QPlainTextEdit::paste);
    pasteAction->setEnabled(canPaste() && !isReadOnly());
    menu.addSeparator();
    menu.addAction(tr("Select All"), this, &QPlainTextEdit::selectAll);
    menu.exec(event->globalPos());
}
