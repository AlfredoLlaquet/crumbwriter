#include "viselection.h"
#include "textkeys.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextLayout>

void ViSelection::prompt(const QString &hint) const
{
    QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
    if (!editor_.onViPrompt) return;
    QString message;
    if (active_) {
        QString display = command_ + preedit_;
        display.replace(u'\n', QChar(0x21b5));
        message = QObject::tr("Movement   %1").arg(display);
        if (!hint.isEmpty()) message += QStringLiteral("   ·   ") + hint;
    }
    editor_.onViPrompt(message);
}

void ViSelection::start()
{
    if (active_) return;
    if (!editor_.textCursor().block().layout()->preeditAreaText().isEmpty())
        QGuiApplication::inputMethod()->commit();
    origin_ = {editor_.textCursor(), editor_.alignment_, editor_.verticalScrollBar()->value(),
               editor_.horizontalScrollBar()->value(), editor_.rows_, editor_.virtualTop_, editor_.topInset_};
    active_ = true;
    prompt(QObject::tr("Type a movement. Enter keeps it. Esc cancels."));
}

void ViSelection::restoreOrigin()
{
    QScopedValueRollback<bool> guard(editor_.editing_, true);
    editor_.setVirtualRows(origin_.virtualRows);
    const auto current = editor_.textCursor();
    if (current.position() != origin_.cursor.position() || current.anchor() != origin_.cursor.anchor())
        editor_.setTextCursor(origin_.cursor);
    editor_.alignment_ = origin_.alignment;
    editor_.virtualTop_ = origin_.virtualTop;
    editor_.setTopInset(origin_.topInset);
    editor_.verticalScrollBar()->setValue(origin_.verticalScroll);
    editor_.horizontalScrollBar()->setValue(origin_.horizontalScroll);
    editor_.viewport()->update();
    preview_.reset();
}

void ViSelection::cancel()
{
    if (!active_) return;
    restoreOrigin();
    abandon();
}

void ViSelection::discardPreedit()
{
    if (preedit_.isEmpty()) return;
    preedit_.clear();
    QScopedValueRollback<bool> guard(discardingPreedit_, true);
    QGuiApplication::inputMethod()->reset();
}

void ViSelection::abandon()
{
    if (!active_) return;
    discardPreedit();
    active_ = false;
    command_.clear();
    preview_.reset();
    origin_ = {};
    prompt();
}

void ViSelection::reset()
{
    abandon();
    lastFind_.clear();
    lastTarget_.clear();
    lastLanding_ = -1;
}

void ViSelection::key(const QKeyEvent &event)
{
    const int key = event.key();
    const auto mods = event.modifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Escape) { cancel(); return; }
    if (key == Qt::Key_Backspace) {
        if (preedit_.isEmpty()) {
            QTextBoundaryFinder chars(QTextBoundaryFinder::Grapheme, command_);
            chars.toEnd();
            command_.truncate(qMax(qsizetype(0), chars.toPreviousBoundary()));
        } else {
            discardPreedit();
        }
        updatePreview();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!preedit_.isEmpty()) QGuiApplication::inputMethod()->commit();
        if (preedit_.isEmpty() && preview_) accept();
        return;
    }
    if (event.matches(QKeySequence::Paste) || (mods == Qt::ControlModifier && key == Qt::Key_V)) {
        auto pasted = QApplication::clipboard()->text();
        command_ += pasted.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(u'\r', u'\n');
    } else if (!event.text().isEmpty() && event.text().front().unicode() >= 0x20 &&
               (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) ||
                isComposedTextKey(event))) {
        command_ += event.text();
    } else {
        return;
    }
    updatePreview();
}

void ViSelection::inputMethod(const QInputMethodEvent &event)
{
    if (discardingPreedit_) return;
    preedit_ = event.preeditString();
    const qsizetype rawStart = command_.size() + event.replacementStart();
    const qsizetype start = qBound(qsizetype(0), rawStart, command_.size());
    const qsizetype end = qBound(start, rawStart + event.replacementLength(), command_.size());
    command_.replace(start, end - start, event.commitString());
    updatePreview();
}

void ViSelection::inputMethodQuery(QInputMethodQueryEvent &event) const
{
    // A composer may replace text before its caret. Its surrounding text is
    // the command being entered, even when the preview spans the manuscript.
    const auto queries = event.queries();
    for (auto query : {Qt::ImSurroundingText, Qt::ImTextBeforeCursor})
        if (queries.testFlag(query)) event.setValue(query, command_);
    for (auto query : {Qt::ImCurrentSelection, Qt::ImTextAfterCursor})
        if (queries.testFlag(query)) event.setValue(query, QString());
    for (auto query : {Qt::ImCursorPosition, Qt::ImAnchorPosition, Qt::ImAbsolutePosition})
        if (queries.testFlag(query)) event.setValue(query, int(command_.size()));
}

void ViSelection::updatePreview()
{
    QString hint;
    auto motion = ViMotion::parse(command_ + preedit_, hint);
    if (motion && (motion->name == ";" || motion->name == ",")) {
        if (lastFind_.isEmpty()) {
            hint = QObject::tr("No previous character find.");
            motion.reset();
        } else {
            const bool reverse = motion->name == ",";
            motion->name = lastFind_;
            if (reverse) motion->name = lastFind_.isUpper() ? lastFind_.toLower() : lastFind_.toUpper();
            motion->target = lastTarget_;
            motion->skipLastCharacter = lastLanding_ == origin_.cursor.position() &&
                                        motion->name.front().isUpper() == landedAfterTarget_;
        }
    }

    if (motion) {
        const auto destination = motion->destination(origin_.cursor);
        if (destination) {
            // Preview leaves typing, undo, and provisional whitespace untouched.
            QScopedValueRollback<bool> guard(editor_.editing_, true);
            auto cursor = origin_.cursor;
            cursor.clearSelection();
            cursor.setPosition(*destination, QTextCursor::KeepAnchor);
            editor_.clearVirtual();
            editor_.clearAlignment();
            editor_.setTextCursor(cursor);
            editor_.ensureCursorVisible();
            preview_ = *motion;
            prompt(QObject::tr("Enter keeps the selection. Esc cancels."));
            return;
        }
        hint = motion->target.isEmpty() ? QObject::tr("End of movement range. Backspace to edit.")
                                        : QObject::tr("Match not found. Backspace to edit.");
    }
    if (preview_) restoreOrigin();
    prompt(hint);
}

void ViSelection::accept()
{
    // Ordinary navigation finishes the preceding typing run. Delaying this
    // until Enter lets Escape return to an untouched provisional separator.
    editor_.finishTyping();
    if (!editor_.pendingSpace_.isNull() && editor_.pendingSpace_.position() != editor_.textCursor().position())
        editor_.trimPending();
    const QChar op = command_.front();
    const bool findChar = QStringLiteral("fFtT").contains(op);
    if (findChar) {
        lastFind_ = preview_->name;
        lastTarget_ = preview_->target;
    }
    lastLanding_ = findChar || op == u';' || op == u',' ? editor_.textCursor().position() : -1;
    landedAfterTarget_ = preview_->name == "f" || preview_->name == "T";
    abandon();
    if (editor_.onViAccepted) editor_.onViAccepted();
}
