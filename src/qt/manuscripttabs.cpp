#include "mainwindow.h"
#include "manuscriptsession.h"
#include "manuscriptpath.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "searchpanel.h"
#include "synonympanel.h"
#include "windowlayout.h"

#include <QApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow *MainWindow::newWindow()
{
    return makeWindow ? makeWindow() : new MainWindow(prefs_);
}

ManuscriptSession *MainWindow::findOpen(const QString &path)
{
    for (auto *widget : QApplication::topLevelWidgets()) {
        auto *window = dynamic_cast<MainWindow *>(widget);
        if (!window || window->allowClose_) continue;
        for (auto *session : window->sessions_) {
            if (ManuscriptPath::same(session->path(), path) || ManuscriptPath::same(session->pendingSavePath_, path) ||
                ManuscriptPath::same(session->store_->draftPath(), path))
                return session;
        }
    }
    return nullptr;
}

bool MainWindow::canWritePath(const ManuscriptSession *writer, const QString &path) const
{
    auto *owner = findOpen(path);
    return !owner || (owner == writer && !ManuscriptPath::same(writer->store_->draftPath(), path));
}

void MainWindow::addSession(ManuscriptSession *session)
{
    session->host_ = this;
    session->setParent(this);
    sessions_.append(session);
    tabs_->addTab(session->editor_, session->label());
    selectSession(session);
}

void MainWindow::newTab()
{
    if (!closeTargets_.isEmpty()) cancelClose();
    static int nextUntitled = 1;
    addSession(new ManuscriptSession(this, prefs_, nextUntitled++));
}

bool MainWindow::openDocumentInTab(const QString &name, bool recovery)
{
    if (auto *other = findOpen(name)) {
        other->host_->selectSession(other);
        other->host_->activateManuscript(false);
        return true;
    }
    // Reuse the pristine launch tab. An untitled draft with pending companion
    // changes is already someone's manuscript, even if its paper is empty.
    auto *previous = active_;
    const bool reuse = previous && tabs_->count() == 1 && previous->path().isEmpty() &&
        !previous->needsConfirmation() && previous->editor_->document()->isEmpty() &&
        previous->store_->draftPath().isEmpty() && previous->pendingSavePath_.isEmpty();
    if (!reuse) newTab();
    auto *opened = active_;
    if (opened->load(name, recovery)) {
        activateManuscript(false);
        return true;
    }
    const QString error = opened->noticeMessage_;
    if (!reuse && previous) {
        removeSession(opened);
        selectSession(previous);
        showNotice(error);
    }
    activateManuscript(false);
    return false;
}

void MainWindow::selectSession(ManuscriptSession *session, bool focus)
{
    if (!session || !sessions_.contains(session)) return;
    tabs_->setCurrentWidget(session->editor_);
    if (focus) session->editor_->setFocus();
}

void MainWindow::currentTabChanged()
{
    auto *page = tabs_->currentWidget();
    ManuscriptSession *next = nullptr;
    for (auto *session : sessions_) {
        if (session->editor_ == page) { next = session; break; }
    }
    if (next == active_) return;
    if (layouts_) layouts_->releaseManuscriptPanels();
    if (active_) {
        auto *old = active_;
        old->searchOpen_ = !old->search_->isHidden();
        old->search_->setParent(old->parkedPanels_);
        old->side_->setParent(old->parkedPanels_);
        old->synonyms_->setParent(old->parkedPanels_);
        old->editor_->setPaperActive(false);
    }
    active_ = next;
    if (!active_) return;
    searchLayout_->addWidget(next->search_);
    leftLayout_->addWidget(next->synonyms_);
    rightLayout_->addWidget(next->side_);
    next->search_->setVisible(next->searchOpen_);
    next->synonyms_->show();
    next->side_->show();
    next->editor_->setPaperActive(isActiveWindow());
    if (layouts_) layouts_->bindManuscriptPanels();
    updateModeControls();
    sessionChanged(next);
    if (isActiveWindow()) prefs_.remember(next->path());
    if (workspaceChanged) workspaceChanged(false);
}

void MainWindow::stepTab(int direction)
{
    const int count = tabs_->count();
    if (!count) return;
    tabs_->setCurrentIndex((tabs_->currentIndex() + direction + count) % count);
    editor()->setFocus();
}

ManuscriptSession *MainWindow::sessionForDialog(QWidget *dialog) const
{
    for (auto *owner = dialog->parentWidget(); owner && owner != this; owner = owner->parentWidget()) {
        for (auto *session : sessions_) {
            if (owner == session->editor_ || owner == session->synonyms_ ||
                owner == session->side_ || owner == session->search_) return session;
        }
    }
    return nullptr;
}

void MainWindow::removeSession(ManuscriptSession *session)
{
    tabs_->removeTab(tabs_->indexOf(session->editor_));
    sessions_.removeOne(session);
    delete session;
}

void MainWindow::moveTabToNewWindow()
{
    if (!active_ || !closeTargets_.isEmpty()) return;
    auto *moved = active_;
    auto *destination = newWindow();
    auto *placeholder = destination->active_;
    // The source remains usable if its only tab is detached.
    if (tabCount() == 1) newTab();
    tabs_->removeTab(tabs_->indexOf(moved->editor_));
    sessions_.removeOne(moved);
    layouts_->transferDocumentTools(moved->editor_, destination->layouts_);
    destination->addSession(moved);
    destination->removeSession(placeholder);
    destination->activateManuscript(false);
}

void MainWindow::rememberClosed(ManuscriptSession *session)
{
    if (session->path().isEmpty()) return;
    const auto cursor = session->editor_->textCursor();
    closedTabs_.append({session->path(), cursor.anchor(), cursor.position(),
                        session->editor_->verticalScrollBar()->value()});
    reopenAction_->setEnabled(true);
}

void MainWindow::reopenClosedTab()
{
    if (closedTabs_.isEmpty()) return;
    const auto record = closedTabs_.last();
    // An already open copy owns its newer selection and scroll position.
    const bool alreadyOpen = findOpen(record.path);
    if (!openDocumentInTab(record.path)) return;
    closedTabs_.removeLast();
    reopenAction_->setEnabled(!closedTabs_.isEmpty());
    if (alreadyOpen) return;
    auto *opened = editor();
    opened->selectRange(record.anchor, record.position, false);
    opened->verticalScrollBar()->setValue(record.scroll);
}

void MainWindow::closeCurrentTab()
{
    if (active_) requestClose({active_}, false);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (allowClose_) { if (workspaceChanged) workspaceChanged(true); event->accept(); return; }
    bool pending = false;
    for (auto *session : sessions_) {
        session->editor_->finishTyping();
        session->editor_->model().confirmMarkerOutside(-1);
        pending |= session->needsConfirmation();
    }
    if (!pending && closeTargets_.isEmpty()) {
        allowClose_ = true;
        if (workspaceChanged) workspaceChanged(true);
        event->accept();
        return;
    }
    event->ignore();
    requestClose(sessions_, true);
}

void MainWindow::requestClose(const QList<ManuscriptSession *> &sessions, bool wholeWindow)
{
    if (!closeTargets_.isEmpty()) {
        if (closeDialog_) { closeDialog_->raise(); closeDialog_->activateWindow(); }
        else if (closingSession_ && closingSession_->chooser_) closingSession_->chooser_->raise();
        return;
    }
    ++closeAttempt_;
    closeTargets_ = sessions;
    closingWindow_ = wholeWindow;
    bool pending = false;
    for (auto *session : sessions) {
        session->editor_->finishTyping();
        session->editor_->model().confirmMarkerOutside(-1);
        if (session->needsConfirmation()) {
            session->store_->beginCloseConfirmation();
            pending = true;
        }
    }
    if (pending) showCloseDialog();
    else continueClose();
}

void MainWindow::refreshCloseDialog()
{
    QStringList names;
    for (auto *s : closeTargets_) {
        if (s->needsConfirmation()) names.append(s->path().isEmpty() ? s->label() : s->path());
    }
    closeDialog_->setText(names.size() <= 1 ? tr("Save the pending changes before closing this manuscript?")
        : tr("Save the pending changes in these %1 manuscripts before closing this window?").arg(names.size()));
    closeDialog_->setInformativeText(names.join(u'\n'));
}

void MainWindow::showCloseDialog()
{
    auto *box = new QMessageBox(QMessageBox::Question, tr("Unsaved Changes"), {}, QMessageBox::NoButton, this);
    closeDialog_ = box;
    box->setObjectName("confirmClose");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::PlainText);
    refreshCloseDialog();
    box->setWindowModality(Qt::NonModal);
    auto *saveButton = box->addButton(closingWindow_ && workspaceChanged && prefs_.enabled("reopen-last") ? tr("Save and Restore Tabs") : tr("Save and Close"), QMessageBox::AcceptRole);
    auto *discard = box->addButton(tr("Discard Unsaved Changes"), QMessageBox::DestructiveRole);
    auto *keep = box->addButton(tr("Keep Editing"), QMessageBox::RejectRole);
    box->setDefaultButton(saveButton);
    box->setEscapeButton(keep);
    connect(box, &QDialog::finished, this, [this, box, discard, saveButton](int) {
        closeDialog_.clear();
        if (box->clickedButton() != discard && box->clickedButton() != saveButton) {
            cancelClose();
            return;
        }
        discarding_ = box->clickedButton() == discard;
        if (discarding_) {
            for (auto *s : closeTargets_) discardRevisions_.insert(s, s->store_->editRevision());
        }
        closeQueue_ = closeTargets_;
        continueClose();
    });
    box->show();
}

void MainWindow::cancelClose()
{
    ++closeAttempt_;
    if (quitCanceled) quitCanceled();
    omittedDrafts_.clear();
    const auto targets = closeTargets_;
    closeTargets_.clear();
    discardRevisions_.clear();
    closeQueue_.clear();
    closingSession_ = nullptr;
    closingWindow_ = false;
    discarding_ = false;
    if (closeDialog_) {
        disconnect(closeDialog_, nullptr, this, nullptr);
        closeDialog_->close();
        closeDialog_.clear();
    }
    for (auto *s : targets) s->store_->keepEditing();
    restoreEditorFocus();
}

void MainWindow::continueClose()
{
    if (discarding_) {
        for (auto it = discardRevisions_.cbegin(); it != discardRevisions_.cend(); ++it) {
            if (it.key()->store_->editRevision() != it.value()) {
                auto *modified = it.key();
                cancelClose();
                selectSession(modified);
                showNotice(tr("Closing stopped because this manuscript changed after you chose Discard."));
                return;
            }
        }
    }
    while (!closeQueue_.isEmpty()) {
        auto *s = closeQueue_.takeFirst();
        s->editor_->finishTyping();
        if (!s->needsConfirmation()) { s->store_->keepEditing(); continue; }
        closingSession_ = s;
        if (discarding_) s->store_->discardAndClose();
        else if (!s->path().isEmpty() && !s->updateCompanions(ManuscriptSession::CompanionAction::Flush)) {
            selectSession(s);
            cancelClose();
        } else {
            // Ask for only one filename at a time. The Save As picker follows
            // this document even if the writer switches tabs before accepting.
            if (s->path().isEmpty()) selectSession(s);
            s->store_->saveAndClose(closingWindow_ && workspaceChanged && prefs_.enabled("reopen-last"));
        }
        return;
    }
    if (!discarding_) {
        // Earlier documents stayed editable while a later file was saving.
        // Include newer revisions before accepting the window close.
        for (auto *s : closeTargets_) {
            const bool kept = closingWindow_ && workspaceChanged && prefs_.enabled("reopen-last") &&
                              s->path().isEmpty() && s->store_->draftIsCurrent() && !s->companionsPending();
            if (s->needsConfirmation() && !kept) closeQueue_.append(s);
        }
        if (!closeQueue_.isEmpty()) { continueClose(); return; }
    }
    const auto targets = closeTargets_;
    closeTargets_.clear();
    discardRevisions_.clear();
    closingSession_ = nullptr;
    discarding_ = false;
    if (closingWindow_) {
        closingWindow_ = false;
        allowClose_ = true;
        close();
        return;
    }
    for (auto *s : targets) {
        rememberClosed(s);
        removeSession(s);
    }
    omittedDrafts_.clear();
    if (sessions_.isEmpty()) newTab();
    else restoreEditorFocus();
}

void MainWindow::closeReady(ManuscriptSession *session)
{
    if (closingSession_ != session) return;
    if (discarding_) {
        if (session->path().isEmpty()) omittedDrafts_.insert(session);
        QString error;
        if (!session->removeDraftConfig(&error)) {
            selectSession(session);
            cancelClose();
            showNotice(error);
            return;
        }
    } else {
        if (!session->updateCompanions(ManuscriptSession::CompanionAction::Flush)) {
            selectSession(session);
            cancelClose();
            return;
        }
        session->store_->keepEditing();
    }
    closingSession_ = nullptr;
    // The store emitted this callback from a queued event on itself. Its last
    // owning tab may be destroyed when the close group is finished.
    QTimer::singleShot(0, this, [this, attempt = closeAttempt_] {
        if (attempt == closeAttempt_ && !closeTargets_.isEmpty()) continueClose();
    });
}
