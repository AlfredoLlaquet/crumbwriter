#include "manuscriptsession.h"
#include "mainwindow.h"
#include "manuscripteditor.h"
#include "textkeys.h"
#include "wordtools.h"

#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTimer>

void ManuscriptSession::cancelCommand()
{
    if (!commandPending()) return;
    commandPrefix_ = CommandPrefix::None;
    changed();
}

void ManuscriptSession::finishCommand(QChar choice)
{
    const auto prefix = commandPrefix_;
    cancelCommand();
    choice = choice.toUpper();
    if (prefix == CommandPrefix::Style) {
        if (choice == u'M') host_->applyLineStyle(-1);
        else if (choice >= u'0' && choice <= u'4') host_->applyLineStyle(choice.unicode() - '0');
        else showNavigationStatus(tr("Style canceled. Use M, 0, or 1–4."));
    } else {
        if (choice == u'D') words_->openWeb(WordTools::Reference::Dictionary);
        else if (choice == u'S') words_->openWeb(WordTools::Reference::Synonyms);
        else if (choice == u'T') words_->openWeb(WordTools::Reference::Thesaurus);
        else showNavigationStatus(tr("Lookup canceled. Use D, S, or T after Ctrl+Shift+L."));
    }
}

bool ManuscriptSession::handleCommandInput(QEvent *event)
{
    if (event->type() == QEvent::FocusOut && commandPending()) {
        // A popup or an input method can briefly move and restore focus.
        QTimer::singleShot(0, this, [this] { if (!editor_->hasFocus()) cancelCommand(); });
        return false;
    }
    if (event->type() == QEvent::InputMethod && commandPending()) {
        const auto &commit = static_cast<QInputMethodEvent *>(event)->commitString();
        if (!commit.isEmpty()) finishCommand(commit.size() == 1 ? commit.front() : QChar());
        event->accept();
        return true;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
    if (editor_->viSelectionActive()) return false;
    const auto &key = *static_cast<QKeyEvent *>(event);
    const auto mods = key.modifiers() & ~Qt::KeypadModifier;
    CommandPrefix prefix = CommandPrefix::None;
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (key.key() == Qt::Key_K && editor_->model().isGeneric()) prefix = CommandPrefix::Style;
        if (key.key() == Qt::Key_L) prefix = CommandPrefix::WebLookup;
    }
    if (!commandPending() && prefix == CommandPrefix::None) return false;
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (prefix != CommandPrefix::None) {
        if (!key.isAutoRepeat()) {
            commandPrefix_ = prefix;
            setStatusMessage(prefix == CommandPrefix::Style
                ? tr("Style: M for Main Title, 0 for Regular Text, 1–4 for section titles")
                : tr("Merriam-Webster — D Dictionary · S Synonyms · T Thesaurus · Esc Cancel"));
        }
        return true;
    }
    if (key.key() == Qt::Key_Escape) {
        cancelCommand();
        return true;
    }
    if (key.isAutoRepeat() || (key.key() >= Qt::Key_Shift && key.key() <= Qt::Key_ScrollLock) ||
        key.key() == Qt::Key_AltGr || ((key.key() == Qt::Key_unknown || !key.key()) && key.text().isEmpty()))
        return true;
    if ((mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && !isComposedTextKey(key)) {
        cancelCommand();
        return false; // Another application command can start immediately.
    }
    if (key.text().size() == 1) finishCommand(key.text().front());
    else if (key.text().isEmpty() && key.key() >= Qt::Key_0 && key.key() <= Qt::Key_Z)
        finishCommand(QChar(static_cast<ushort>(key.key())));
    else finishCommand({});
    return true;
}
