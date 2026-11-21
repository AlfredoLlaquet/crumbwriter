#include "manuscripteditor.h"
#include "committedpunctuation.h"
#include "manuscripttext.h"
#include "viselection.h"

#include <climits>

#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopedValueRollback>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QTextBlock>
#include <QtWidgets/QMenu>

namespace {
bool horizontal(QChar ch) { return ch == u' ' || ch == u'\t'; }
bool newline(QChar ch) { return ch == u'\n' || ch == QChar::ParagraphSeparator; }
}

QString ManuscriptEditor::normalizeInsertion(QString text, bool keepFinalSpace)
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

void ManuscriptEditor::trackSeparator(const QTextCursor &cursor)
{
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
        punctuation_->feed(document(), first, text);
    return true;
}

void ManuscriptEditor::recordEditForHash(QTextCursor &cursor, const QString &input)
{
    // Pasted hashes are literal. Only an actual typed hash can continue a
    // number. A multi-character IM commit ending in a hash follows the same rule.
    if (input.endsWith(u'#') && cursor.positionInBlock() > 0) {
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
    if (replaceStart || replaceLength) {
        if (c.hasSelection())
            c.removeSelectedText();
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
    // A discarded duplicate space creates no undo command to join later.
    if (replaceAt(c, text, Input::Typed)) editRun_ = run;
    else if (editRun_ != run) editRun_ = EditRun::None;
    recordEditForHash(c, text);
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
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
    replaceAt(c, text, kind);
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
}

int ManuscriptEditor::replaceRangeLiteral(int start, int end, const QString &text)
{
    // Search may use this inside an outer edit block spanning many ranges.
    if (isReadOnly())
        return start;
    auto c = at(start);
    c.setPosition(clampPosition(end), QTextCursor::KeepAnchor);
    finishTyping();
    QScopedValueRollback<bool> guard(editing_, true);
    c.beginEditBlock();
    replaceAt(c, text, Input::Replacement);
    c.endEditBlock();
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
    QGuiApplication::clipboard()->setMimeData(createMimeDataFromSelection());
    insertSelection({}, Input::Erase);
}

void ManuscriptEditor::replayEdit(bool forward)
{
    QScopedValueRollback<bool> guard(replaying_, true);
    editRun_ = EditRun::None;
    punctuation_->reset();
    pendingSpace_ = {};
    clearVirtual();
    clearAlignment();
    QTextCursor restored;
    // Undo can restore a provisional separator in an earlier crumb and
    // remove an appended crumb in the same action. Qt's generic undo cursor
    // points at the last insertion. Restore the actual writing position.
    const auto observed = connect(document(), &QTextDocument::contentsChange, this,
        [this, &restored](int pos, int, int added) {
            const int end = clampPosition(pos + added);
            for (auto block = document()->findBlock(pos); added && block.isValid()
                 && block.position() <= end; block = block.next()) {
                const int after = block.position() + block.length() - 1;
                if (after > pos && after <= end && document()->characterAt(after-1) == u' ') {
                    restored = at(after);
                    break;
                }
            }
        });
    if (forward)
        QPlainTextEdit::redo();
    else
        QPlainTextEdit::undo();
    disconnect(observed);
    if (!restored.isNull())
        setTextCursor(restored);
    trackSeparator(textCursor());
}

void ManuscriptEditor::undo() { replayEdit(false); }
void ManuscriptEditor::redo() { replayEdit(true); }

void ManuscriptEditor::removeByKey(QKeyEvent *event)
{
    const bool backward = event->key() == Qt::Key_Backspace;
    const bool byWord = event->modifiers().testFlag(Qt::ControlModifier);
    auto c = textCursor();
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
        // A punctuation mark or emoji outside a word is one grapheme. Internal
        // apostrophes and hyphens are part of the word on either side of the caret.
        static const QRegularExpression tokens(QStringLiteral(
            R"([\p{L}\p{M}\p{N}_]+(?:['’ʼ‐‑-][\p{L}\p{M}\p{N}_]+)*|\h+|\X)"));
        const auto text = ManuscriptText::blockText(c.block());
        if (text.isEmpty())
            return;
        const int here = qMin(c.positionInBlock(), int(text.size()) - 1);
        auto matches = tokens.globalMatch(text);
        while (matches.hasNext()) {
            const auto token = matches.next();
            if (token.capturedEnd() <= here)
                continue;
            const int start = c.block().position();
            c.setPosition(start + int(token.capturedStart()));
            c.setPosition(start + int(token.capturedEnd()), QTextCursor::KeepAnchor);
            break;
        }
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
    replaceAt(c, {}, Input::Erase);
    editRun_ = run;
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
}

void ManuscriptEditor::inputMethodEvent(QInputMethodEvent *e)
{
    if (vi_->active()) {
        vi_->inputMethod(*e);
        e->accept();
        return;
    }
    if (e->commitString().isEmpty() && !e->replacementLength()) {
        QPlainTextEdit::inputMethodEvent(e);
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
        QPlainTextEdit::inputMethodEvent(&preedit);
    }
    e->accept();
}

bool ManuscriptEditor::event(QEvent *e)
{
    if (vi_ && vi_->active()) {
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
    if (enter && mods == Qt::ControlModifier) { centerCurrentLine(); return; }
    if (enter && mods == Qt::AltModifier) { topCurrentCrumb(); return; }
    if ((mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier)) &&
        moveByControlKey(key, mods.testFlag(Qt::ShiftModifier))) return;
    if (mods == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_Q: copyCrumb(); return;
        case Qt::Key_1: copyHeader(); return;
        case Qt::Key_Y: deleteCrumb(); return;
        case Qt::Key_W: deleteWord(); return;
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
        removeByKey(e);
        return;
    }
    if (enter && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        typeText(QStringLiteral("\n"));
        return;
    }
    const bool altGr = mods.testFlag(Qt::GroupSwitchModifier) ||
                      (mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::AltModifier));
    if (!e->text().isEmpty() && e->text().front().unicode() >= 0x20 &&
        (altGr || !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))) {
        typeText(e->text());
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

void ManuscriptEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    if (spellingMenu) {
        const int pos = event->reason() == QContextMenuEvent::Keyboard ? textCursor().position()
                            : cursorForPosition(event->pos()).position();
        spellingMenu(&menu, pos);
        if (!menu.isEmpty())
            menu.addSeparator();
    }
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
