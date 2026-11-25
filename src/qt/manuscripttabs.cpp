#include "mainwindow.h"
#include "manuscriptsession.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "searchpanel.h"
#include "synonympanel.h"
#include "windowlayout.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
bool sameFile(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    const QFileInfo first(a), second(b);
    if (first == second) return true;
    const QString canonical = first.canonicalFilePath();
    return !canonical.isEmpty() && canonical == second.canonicalFilePath();
}
}

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
            if (sameFile(session->path(), path) || sameFile(session->pendingSavePath_, path))
                return session;
        }
    }
    return nullptr;
}

bool MainWindow::canWritePath(const ManuscriptSession *writer, const QString &path) const
{
    for (auto *widget : QApplication::topLevelWidgets()) {
        auto *window = dynamic_cast<MainWindow *>(widget);
        if (!window) continue;
        for (auto *s : window->sessions_) if (sameFile(s->store_->draftPath(), path)) return false;
    }
    auto *other = findOpen(path);
    return !other || other == writer;
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
    if (!recovery) {
        if (auto *other = findOpen(name)) {
            other->host_->selectSession(other);
            other->host_->activateManuscript(false);
            return true;
        }
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
    if (allowClose_) { event->accept(); return; }
    bool pending = false;
    for (auto *session : sessions_) {
        session->editor_->finishTyping();
        session->editor_->model().confirmMarkerOutside(-1);
        pending |= session->needsConfirmation();
    }
    if (!pending && closeTargets_.isEmpty()) {
        allowClose_ = true;
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
    QStringList pending;
    for (auto *session : sessions) {
        session->editor_->finishTyping();
        session->editor_->model().confirmMarkerOutside(-1);
        if (session->needsConfirmation()) {
            session->store_->beginCloseConfirmation();
            pending.append(session->path().isEmpty() ? session->label() : session->path());
        }
    }
    if (pending.isEmpty()) continueClose();
    else showCloseDialog(pending);
}

void MainWindow::showCloseDialog(const QStringList &pendingNames)
{
    const QString question = pendingNames.size() == 1 ? tr("Save the pending changes before closing this manuscript?")
        : tr("Save the pending changes in these %1 manuscripts before closing this window?").arg(pendingNames.size());
    auto *box = new QMessageBox(QMessageBox::Question, tr("Unsaved Changes"), question, QMessageBox::NoButton, this);
    closeDialog_ = box;
    box->setObjectName("confirmClose");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::PlainText);
    box->setInformativeText(pendingNames.join(u'\n'));
    box->setWindowModality(Qt::NonModal);
    auto *saveButton = box->addButton(tr("Save and Close"), QMessageBox::AcceptRole);
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
            for (auto *s : closeTargets_) discardRevisions_.insert(s, s->editor_->document()->revision());
        }
        closeQueue_ = closeTargets_;
        continueClose();
    });
    box->show();
}

void MainWindow::cancelClose()
{
    ++closeAttempt_;
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
    if (active_) editor()->setFocus();
}

void MainWindow::continueClose()
{
    if (discarding_) {
        for (auto it = discardRevisions_.cbegin(); it != discardRevisions_.cend(); ++it) {
            if (it.key()->editor_->document()->revision() != it.value()) {
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
            s->store_->saveAndClose();
        }
        return;
    }
    if (!discarding_) {
        // Earlier documents stayed editable while a later file was saving.
        // Include newer revisions before accepting the window close.
        for (auto *s : closeTargets_) if (s->needsConfirmation()) closeQueue_.append(s);
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
    if (sessions_.isEmpty()) newTab();
    else editor()->setFocus();
}

void MainWindow::closeReady(ManuscriptSession *session)
{
    if (closingSession_ != session) return;
    if (discarding_) {
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
