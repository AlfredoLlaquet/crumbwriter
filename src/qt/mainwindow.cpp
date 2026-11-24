#include "mainwindow.h"
#include "wordtools.h"
#include <QDesktopServices>
#include <QGridLayout>
#include <QResizeEvent>
#include <QUrl>
#include "commandicons.h"
#include "bookmarks.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "searchpanel.h"
#include "spellcheck.h"
#include "synonympanel.h"
#include "synonymwriting.h"
#include "windowlayout.h"

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
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(Preferences &preferences, QWidget *parent)
    : QWidget(parent), prefs_(preferences)
{
    prefs_.applyControlTheme();
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
    dismiss->setFocusPolicy(Qt::StrongFocus);
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
    synonyms_ = new SynonymPanel(editor_->document(), this);
    split_->addWidget(synonyms_);
    split_->addWidget(editor_);
    side_ = new NavigationPanel(editor_, &prefs_.settings(), this);
    split_->addWidget(side_);
    split_->setChildrenCollapsible(false);
    for (int i = 0; i < split_->count(); ++i) split_->setStretchFactor(i, 1);
    layout->addWidget(split_, 1);

    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("documentStatus");
    status_->setMargin(5);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer_ = new QGridLayout;
    footer_->setContentsMargins(0, 0, 0, 0);
    footer_->setHorizontalSpacing(0);
    footer_->setColumnStretch(0, 1);
    footer_->setColumnStretch(2, 1);
    footer_->addWidget(status_, 0, 0);
    website_ = new QToolButton(this);
    website_->setObjectName("crumbsWebsite");
    website_->setText(QStringLiteral("Read Alfredo Llaquet-Alsina’s Crumbs at TheCrumbs.org"));
    website_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    website_->setAutoRaise(true);
    website_->setFocusPolicy(Qt::StrongFocus);
    website_->setCursor(Qt::PointingHandCursor);
    website_->setToolTip(QStringLiteral("Open TheCrumbs.org in your browser"));
    website_->setStyleSheet("QToolButton { color: palette(window-text); background: transparent; border: none; padding: 3px; }"
                            "QToolButton:hover, QToolButton:focus { text-decoration: underline; }");
    connect(website_, &QToolButton::clicked, this, &MainWindow::openCrumbsWebsite);
    footer_->addWidget(website_, 0, 1, Qt::AlignCenter);
    mode_ = new QLabel(this);
    mode_->setTextFormat(Qt::PlainText);
    mode_->setObjectName("documentMode");
    mode_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mode_->setMargin(5);
    footer_->addWidget(mode_, 0, 2);
    layout->addLayout(footer_);

    store_ = new DocumentStore(this);
    editor_->onViPrompt = [this](const QString &prompt) {
        setStatusMessage(prompt.isEmpty() ? store_->status() : prompt);
    };
    store_->setSnapshotProvider([this] { return editor_->manuscriptText(); });
    store_->captureMetadata = [this] {
        bookmarks_->captureForSave();
        return editor_->model().dailySnapshot();
    };
    store_->onPublished = [this](const QString &path, const QByteArray &metadata) {
        const QString error = editor_->model().publishDailySnapshot(path, metadata);
        if (!error.isEmpty()) showNotice(error);
        if (path == store_->path()) {
            QString bookmarkError;
            if (!bookmarks_->publishSave(&bookmarkError)) showNotice(bookmarkError);
        }
    };
    bookmarks_ = new Bookmarks(this,
        [this] { return editor_->textCursor().blockNumber() + 1 + editor_->virtualRows(); },
        editor_->document(),
        [this](int line) {
            const auto &model = editor_->model();
            const auto &record = model.record(line - 1);
            return model.isGeneric() ? record.text : record.title;
        },
        [this](int line) {
            const auto block = editor_->document()->findBlockByNumber(line - 1);
            if (block.isValid()) jump(block.position());
        });
    spelling_ = new SpellCheck(editor_, this);
    synonymWriting_ = new SynonymWriting(editor_, synonyms_->matcher());
    words_ = new WordTools(editor_);
    words_->onNotice = [this](const QString &message) { showNavigationStatus(message); };
    connectWritingTools();
    editor_->model().onDailyCountError = [this](const QString &error) { showNotice(error); };
    store_->onChanged = [this] { documentStateChanged(); };
    store_->onError = [this](const QString &error) { showNotice(error); };
    store_->onPathChanged = [this](const QString &, const QString &target) {
        if (!loading_) {
            const QString dailyError = editor_->model().saveDailyCountAs(target);
            if (!dailyError.isEmpty()) showNotice(dailyError);
            if (!updateCompanions(CompanionAction::SaveAs, target)) store_->keepEditing();
        }
        prefs_.remember(target);
        documentStateChanged();
    };
    store_->onSaveAsNeeded = [this] { chooseFile(true); };
    store_->onReadyToClose = [this] {
        if (!discarding_ && !updateCompanions(CompanionAction::Flush)) {
            store_->keepEditing();
            return;
        }
        allowClose_ = true;
        close();
    };
    side_->jumpTo = [this](int pos) { jump(pos); };
    search_->recordOrigin = [this](const QTextCursor &c) { trail_.remember(c); };
    editor_->model().observe(this, [this](const CrumbChange &change) {
        if (change.modeChanged) updateModeControls();
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
    updateModeControls();
    const QSize available = screen()->availableGeometry().size();
    resize(QSize(1060, 720).boundedTo(available * 0.80));
    const int left = qMin(synonymWidth_, width() / 5);
    const int right = qMin(sidebarWidth_, width() / 4);
    split_->setSizes({left, qMax(1, width() - left - right), right});
    qApp->installEventFilter(this);
    applyPreferences();
    layouts_ = new WindowLayouts(this, prefs_.settings());
    documentStateChanged();
    editor_->setFocus();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    editor_->onViPrompt = {};
    editor_->model().onDailyCountError = {};
    store_->onChanged = {};
    store_->onError = {};
    store_->onSaveAsNeeded = {};
    store_->onReadyToClose = {};
    store_->onPathChanged = {};
    store_->captureMetadata = {};
    store_->onPublished = {};
    bookmarks_->onChanged = {};
    synonyms_->onChanged = {};
    delete synonymWriting_; // Disconnect from the matcher while both panels are alive.
    delete spelling_; // Its viewport callbacks belong to the still-live editor.
}

bool MainWindow::loadDocument(const QString &name, bool recovery)
{
    loading_ = true;
    QString text, error;
    const bool ok = recovery ? store_->openDraft(name, &text, &error)
                             : store_->open(name, prefs_.enabled("backup"), &text, &error);
    if (ok) {
        showNotice({});
        trail_.clear();
        search_->reset();
        words_->closeThesaurus();
        webLookupPending_ = false;
        editor_->loadText(text);
        const QString dailyError = editor_->model().openDailyCount(recovery ? name : store_->path());
        if (!dailyError.isEmpty()) showNotice(dailyError);
        side_->reset();
        updateCompanions(CompanionAction::Open, store_->path());
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
    const QPalette p = prefs_.applyControlTheme();
    editor_->setPaperPalette(p);
    editor_->setPaperActive(isActiveWindow());
    const QFont font = prefs_.manuscriptFont();
    if (editor_->font() != font)
        editor_->setFont(font);
    editor_->setSmartPunctuation(prefs_.enabled("smart-punctuation"));
    editor_->setCopyDeletedText(prefs_.enabled("copy-deleted"));
    synonymWriting_->applySettings(prefs_.synonymSettings());
    spelling_->setEnabled(prefs_.enabled("spell-check"));
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
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (mode_) fitFooter();
}

void MainWindow::setStatusMessage(const QString &message)
{
    status_->setText(message);
    fitFooter();
}

void MainWindow::fitFooter()
{
    // Equal side columns center the full link on the window. At smaller widths
    // give the status and mode the space they need, including movement prompts.
    const int side = qMax(status_->fontMetrics().horizontalAdvance(status_->text()) + 12,
                          mode_->sizeHint().width());
    const bool fits = website_->sizeHint().width() + 2 * side + 12 <= width()
                      && !editor_->viSelectionActive() && !webLookupPending_;
    if (fits == !website_->isHidden()) return;
    website_->setVisible(fits);
    footer_->setColumnStretch(2, fits ? 1 : 0);
}

void MainWindow::openCrumbsWebsite()
{
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.thecrumbs.org/"))))
        showNotice(QStringLiteral("Could not open TheCrumbs.org in your browser."));
}

void MainWindow::documentStateChanged()
{
    const QString name = store_->path().isEmpty() ? QString("Untitled") : QFileInfo(store_->path()).fileName();
    const bool pending = store_->isDirty() || companionsPending();
    const QString title = (pending ? QString("• ") : QString()) + name + " — Crumb Writer";
    if (title != windowTitle()) setWindowTitle(title);
    if (!editor_->viSelectionActive() && !webLookupPending_)
        setStatusMessage(synonymHint_.isEmpty() ? store_->status() : synonymHint_);
    status_->setToolTip(store_->backupPath().isEmpty() ? store_->path()
                       : store_->path() + "\nStartup backup: " + store_->backupPath());
}

void MainWindow::showNotice(const QString &message)
{
    notice_->setText(message);
    noticeBar_->setVisible(!message.isEmpty());
}

void MainWindow::showNavigationStatus(const QString &message)
{
    setStatusMessage(message);
    QTimer::singleShot(2500, this, [this, message] {
        if (status_->text() == message && !editor_->viSelectionActive() && !webLookupPending_)
            setStatusMessage(synonymHint_.isEmpty() ? store_->status() : synonymHint_);
    });
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
    if (saving) dialog->selectFile(path().isEmpty()
        ? (editor_->model().isGeneric() ? "Manuscript.txt" : "Crumbs.txt")
        : QFileInfo(path()).fileName());
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
    CommandIcons::setFileDialogIcons(dialog);
}

void MainWindow::save(bool asNew)
{
    editor_->finishTyping();
    if (asNew || path().isEmpty()) { chooseFile(true); return; }
    updateCompanions(CompanionAction::Flush);
    store_->save();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (allowClose_) { e->accept(); return; }
    editor_->finishTyping();
    if (!store_->needsCloseConfirmation() && !companionsPending()) {
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
            if (!path().isEmpty() && !updateCompanions(CompanionAction::Flush)) {
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
