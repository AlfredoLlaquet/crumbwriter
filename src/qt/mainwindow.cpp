#include "mainwindow.h"
#include "bookmarks.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "searchpanel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(Preferences &preferences, QWidget *parent)
    : QWidget(parent), prefs_(preferences)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("manuscriptWindow");
    setMinimumSize(1, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    buildToolbar(layout);

    noticeBar_ = new QWidget(this);
    auto *noticeLayout = new QHBoxLayout(noticeBar_);
    noticeLayout->setContentsMargins(8, 3, 6, 3);
    notice_ = new QLabel(noticeBar_);
    notice_->setObjectName("notice");
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    notice_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    noticeLayout->addWidget(notice_, 1);
    auto *dismiss = new QToolButton(noticeBar_);
    dismiss->setText("×");
    dismiss->setAccessibleName("Dismiss message");
    connect(dismiss, &QToolButton::clicked, noticeBar_, &QWidget::hide);
    noticeLayout->addWidget(dismiss);
    layout->addWidget(noticeBar_);
    noticeBar_->hide();

    editor_ = new ManuscriptEditor(this);
    editor_->setObjectName("manuscript");
    editor_->setAccessibleName("Manuscript");
    search_ = new SearchPanel(editor_, &prefs_.settings(), this);
    layout->addWidget(search_);
    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setObjectName("manuscriptSplitter");
    split_->addWidget(editor_);
    side_ = new NavigationPanel(editor_, split_);
    split_->addWidget(side_);
    split_->setCollapsible(0, false);
    split_->setCollapsible(1, false);
    split_->setStretchFactor(0, 1);
    split_->setStretchFactor(1, 0);
    layout->addWidget(split_, 1);

    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("documentStatus");
    status_->setMargin(5);
    layout->addWidget(status_);

    store_ = new DocumentStore(this);
    store_->setSnapshotProvider([this] { return editor_->manuscriptText(); });
    bookmarks_ = new Bookmarks(this,
        [this] { return editor_->textCursor().blockNumber() + 1 + editor_->virtualRows(); },
        [this] { return editor_->document()->blockCount(); },
        [this](int line) { return editor_->model().record(line - 1).title; },
        [this](int line) {
            const auto block = editor_->document()->findBlockByNumber(line - 1);
            if (block.isValid()) jump(block.position());
        });
    bookmarks_->onChanged = [this] { documentStateChanged(); };
    store_->onChanged = [this] { documentStateChanged(); };
    store_->onError = [this](const QString &error) { showNotice(error); };
    store_->onPathChanged = [this](const QString &, const QString &target) {
        if (!loading_) {
            QString error;
            if (!bookmarks_->saveAs(target, &error)) {
                store_->keepEditing();
                showNotice(error);
            }
        }
        prefs_.remember(target);
        documentStateChanged();
    };
    store_->onSaveAsNeeded = [this] { chooseFile(true); };
    store_->onReadyToClose = [this] {
        if (!discarding_ && !flushBookmarks()) {
            store_->keepEditing();
            return;
        }
        allowClose_ = true;
        close();
    };
    side_->jumpTo = [this](int pos) { jump(pos); };
    search_->recordOrigin = [this](const QTextCursor &c) { trail_.remember(c); };
    editor_->model().observe(this, [this](const CrumbChange &change) {
        if (!loading_ && change.textChanged) {
            store_->markEdited();
            bookmarks_->refresh();
            if (isActiveWindow()) prefs_.remember(store_->path());
        }
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (!loading_ && isActiveWindow()) prefs_.remember(store_->path());
    });

    buildMenu();
    const QSize available = screen()->availableGeometry().size();
    resize(QSize(1060, 720).boundedTo(available * 0.80));
    split_->setSizes({qMax(1, width() - sidebarWidth_), sidebarWidth_});
    qApp->installEventFilter(this);
    applyPreferences();
    documentStateChanged();
    editor_->setFocus();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    store_->onChanged = {};
    store_->onError = {};
    store_->onSaveAsNeeded = {};
    store_->onReadyToClose = {};
    store_->onPathChanged = {};
    bookmarks_->onChanged = {};
}

bool MainWindow::loadDocument(const QString &name, bool recovery)
{
    loading_ = true;
    QString text, error;
    const bool ok = recovery ? store_->openDraft(name, &text, &error)
                             : store_->open(name, prefs_.enabled("backup"), &text, &error);
    if (ok) {
        trail_.clear();
        search_->reset();
        editor_->loadText(text);
        side_->reset();
        if (!bookmarks_->openManuscript(store_->path(), &error)) showNotice(error);
        if (recovery) showNotice("Recovered an unsaved manuscript. Use Save As to give it a filename.");
        prefs_.remember(store_->path());
    } else {
        showNotice(error);
    }
    loading_ = false;
    documentStateChanged();
    return ok;
}

QString MainWindow::path() const { return store_->path(); }

void MainWindow::activateManuscript(bool goToEnd)
{
    if (goToEnd) editor_->openAtEnd();
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
    editor_->setFocus();
    prefs_.remember(store_->path());
}

void MainWindow::applyPreferences()
{
    const QPalette p = prefs_.paletteForDesktop();
    setPalette(p);
    editor_->setPaperPalette(p);
    editor_->setPaperActive(isActiveWindow());
    const QFont font = prefs_.manuscriptFont();
    if (editor_->font() != font) {
        editor_->setFont(font);
        editor_->document()->setDefaultFont(font);
    }
    editor_->setSmartPunctuation(prefs_.enabled("smart-punctuation"));
    store_->configureLiveSave(prefs_.enabled("live-save"), prefs_.saveDelay());
    side_->updateCurrent();
}

void MainWindow::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (!editor_ || !store_) return;
    if (e->type() == QEvent::ActivationChange) {
        editor_->setPaperActive(isActiveWindow());
        if (isActiveWindow() && !loading_) prefs_.remember(store_->path());
    } else if (e->type() == QEvent::ApplicationPaletteChange) {
        applyPreferences();
    }
}

void MainWindow::documentStateChanged()
{
    const QString name = store_->path().isEmpty() ? QString("Untitled") : QFileInfo(store_->path()).fileName();
    const bool pending = store_->isDirty() || bookmarks_->hasPending();
    const QString title = (pending ? QString("• ") : QString()) + name + " — Crumb Writer";
    if (title != windowTitle()) setWindowTitle(title);
    status_->setText(store_->status());
    status_->setToolTip(store_->backupPath().isEmpty() ? store_->path()
                       : store_->path() + "\nStartup backup: " + store_->backupPath());
}

void MainWindow::showNotice(const QString &message)
{
    notice_->setText(message);
    noticeBar_->setVisible(!message.isEmpty());
}

bool MainWindow::flushBookmarks()
{
    if (!bookmarks_->hasPending()) return true;
    QString error;
    if (bookmarks_->flush(&error)) return true;
    showNotice(error);
    return false;
}

void MainWindow::chooseFile(bool saving)
{
    if (chooser_) { chooser_->raise(); chooser_->activateWindow(); return; }
    auto *dialog = new QFileDialog(this, saving ? "Save Manuscript As" : "Open Manuscript");
    chooser_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setAcceptMode(saving ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog->setFileMode(saving ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog->setNameFilters({"Text files (*.txt)", "All files (*)"});
    if (!path().isEmpty()) dialog->setDirectory(QFileInfo(path()).absolutePath());
    if (saving) dialog->selectFile(path().isEmpty() ? "Crumbs.txt" : QFileInfo(path()).fileName());
    connect(dialog, &QFileDialog::fileSelected, this, [this, saving](const QString &name) {
        if (!saving) { if (openRequested) openRequested(name); return; }
        if (pathAvailable && !pathAvailable(name)) {
            store_->keepEditing();
            showNotice("That manuscript is already open in another Crumb Writer window.");
            return;
        }
        editor_->finishTyping();
        store_->saveAs(name, prefs_.enabled("backup"));
    });
    if (saving) connect(dialog, &QDialog::rejected, store_, &DocumentStore::keepEditing);
    dialog->show();
}

void MainWindow::save(bool asNew)
{
    editor_->finishTyping();
    if (asNew || path().isEmpty()) { chooseFile(true); return; }
    flushBookmarks();
    store_->save();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (allowClose_) { e->accept(); return; }
    editor_->finishTyping();
    if (!store_->needsCloseConfirmation() && !bookmarks_->hasPending()) {
        e->accept();
        return;
    }
    e->ignore();
    if (closeDialog_) { closeDialog_->raise(); closeDialog_->activateWindow(); return; }
    store_->beginCloseConfirmation();
    showCloseDialog();
}

void MainWindow::showCloseDialog()
{
    auto *box = new QMessageBox(QMessageBox::Question, "Unsaved Changes",
        "Save the pending changes before closing this manuscript?", QMessageBox::NoButton, this);
    closeDialog_ = box;
    box->setObjectName("confirmClose");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::NonModal);
    auto *saveButton = box->addButton("Save and Close", QMessageBox::AcceptRole);
    auto *discard = box->addButton("Discard Unsaved Changes", QMessageBox::DestructiveRole);
    auto *keep = box->addButton("Keep Editing", QMessageBox::RejectRole);
    box->setDefaultButton(saveButton);
    box->setEscapeButton(keep);
    connect(box, &QDialog::finished, this, [this, box, saveButton, discard](int) {
        closeDialog_.clear();
        if (box->clickedButton() == discard) {
            discarding_ = true;
            store_->discardAndClose();
        } else if (box->clickedButton() == saveButton) {
            if (!path().isEmpty() && !flushBookmarks()) {
                store_->keepEditing();
                return;
            }
            store_->saveAndClose();
        } else {
            store_->keepEditing();
            editor_->setFocus();
        }
    });
    box->show();
}
