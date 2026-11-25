#include "mainwindow.h"
#include "manuscriptsession.h"
#include "documentstore.h"
#include "editorcolors.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "searchpanel.h"
#include "synonympanel.h"
#include "windowlayout.h"

#include <QApplication>
#include <QDesktopServices>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>

namespace {
QVBoxLayout *paneLayout(QWidget *widget)
{
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return layout;
}
}

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
    connect(dismiss, &QToolButton::clicked, this, [this] { showNotice({}); });
    noticeLayout->addWidget(dismiss);
    layout->addWidget(noticeBar_);
    noticeBar_->hide();

    searchLayout_ = new QVBoxLayout;
    searchLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(searchLayout_);
    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setObjectName("manuscriptSplitter");
    leftPane_ = new QWidget(split_);
    leftPane_->setObjectName("synonymPanel");
    leftPane_->setAccessibleName(tr("Synonym lists"));
    leftLayout_ = paneLayout(leftPane_);
    tabs_ = new QTabWidget(split_);
    // Preserve the manuscript pane's saved layout identity across upgrades.
    tabs_->setObjectName("manuscript");
    tabs_->setAccessibleName(tr("Manuscript"));
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);
    tabs_->setTabsClosable(true);
    tabs_->tabBar()->setObjectName("manuscriptTabs");
    tabs_->tabBar()->setAccessibleName(tr("Open manuscripts"));
    tabs_->tabBar()->setExpanding(false);
    tabs_->setElideMode(Qt::ElideMiddle);
    connect(tabs_->tabBar(), &QTabBar::tabBarClicked, this, [this](int index) {
        if (auto *page = tabs_->widget(index)) page->setFocus(Qt::MouseFocusReason);
    });
    rightPane_ = new QWidget(split_);
    rightPane_->setObjectName("sidebar");
    rightPane_->setAccessibleName(tr("Navigation"));
    rightLayout_ = paneLayout(rightPane_);
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

    buildMenu();
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { currentTabChanged(); });
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        for (auto *session : sessions_) {
            if (session->editor_ == tabs_->widget(index)) { requestClose({session}, false); return; }
        }
    });
    newTab();
    const QSize available = screen()->availableGeometry().size();
    resize(QSize(1060, 720).boundedTo(available * 0.80));
    const int left = qMin(synonymWidth_, width() / 5);
    const int right = qMin(sidebarWidth_, width() / 4);
    split_->setSizes({left, qMax(1, width() - left - right), right});
    qApp->installEventFilter(this);
    layouts_ = new WindowLayouts(this, prefs_.settings());
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    delete layouts_;
    layouts_ = nullptr;
    tabs_->disconnect(this);
    active_ = nullptr;
    qDeleteAll(sessions_);
}

ManuscriptEditor *MainWindow::editor() const { return active_ ? active_->editor_ : nullptr; }
QString MainWindow::path() const { return active_ ? active_->path() : QString(); }
int MainWindow::tabCount() const { return tabs_->count(); }

bool MainWindow::loadDocument(const QString &path, bool recovery)
{
    if (!recovery) {
        if (auto *other = findOpen(path); other && other != active_) {
            other->host_->selectSession(other);
            other->host_->activateManuscript(false);
            return true;
        }
    }
    if (active_->companionsPending()) {
        showNotice(tr("Save this manuscript’s pending companion files before reloading it."));
        return false;
    }
    return active_->load(path, recovery);
}

void MainWindow::restoreEditorFocus(QWidget *finishedDialog)
{
    if (!active_ || !isVisible()) return;
    auto *foreground = QApplication::activeWindow();
    if (foreground && foreground != this && foreground != finishedDialog && foreground->isVisible()) return;
    activateWindow();
    editor()->setFocus();
}

void MainWindow::activateManuscript(bool goToEnd)
{
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
    if (goToEnd || (active_->openingCaret_ == editor()->textCursor().position() && !editor()->textCursor().hasSelection()))
        editor()->openAtEnd();
    active_->openingCaret_ = -1;
    editor()->setFocus();
    prefs_.remember(path());
}

void MainWindow::applyPreferences()
{
    const QPalette palette = prefs_.applyControlTheme();
    for (auto *session : sessions_) session->applyPreferences(palette);
    if (active_) updateFooter();
}

void MainWindow::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (!active_) return;
    if (e->type() == QEvent::ActivationChange) {
        editor()->setPaperActive(isActiveWindow());
        if (isActiveWindow() && !active_->loading_) prefs_.remember(path());
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    allowClose_ = false;
    if (!active_ || active_->openingCaret_ < 0) return;
    // A tab page can receive its first Show before its QTabWidget gets the
    // final content rectangle. Clamp the EOF viewport after the window maps.
    QTimer::singleShot(0, this, [this, opened = QPointer<ManuscriptSession>(active_)] {
        if (!opened || opened != active_ || opened->openingCaret_ < 0) return;
        if (editor()->textCursor().position() == opened->openingCaret_ && !editor()->textCursor().hasSelection())
            editor()->openAtEnd();
        opened->openingCaret_ = -1;
    });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (mode_) fitFooter();
}

void MainWindow::fitFooter()
{
    // Equal side columns center the full link on the window. At smaller widths
    // give the status and mode the space they need, including movement prompts.
    const int side = qMax(status_->fontMetrics().horizontalAdvance(status_->text()) + 12,
                          mode_->sizeHint().width());
    const bool fits = website_->sizeHint().width() + 2 * side + 12 <= width()
                      && active_ && !editor()->viSelectionActive() && !active_->webLookupPending_;
    if (fits == !website_->isHidden()) return;
    website_->setVisible(fits);
    footer_->setColumnStretch(2, fits ? 1 : 0);
}

void MainWindow::openCrumbsWebsite()
{
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.thecrumbs.org/"))))
        showNotice(QStringLiteral("Could not open TheCrumbs.org in your browser."));
}

void MainWindow::showNotice(const QString &message)
{
    if (active_) active_->showNotice(message);
}

void MainWindow::sessionChanged(ManuscriptSession *session)
{
    const int index = tabs_->indexOf(session->editor_);
    if (index < 0) return;
    const bool pending = session->store_->isDirty() || session->companionsPending();
    if (closeDialog_ && closeTargets_.contains(session)) refreshCloseDialog();
    const QString label = (pending ? QStringLiteral("• ") : QString()) + session->label();
    // Ampersands in filenames are literal, including in tab labels.
    QString tabLabel = label;
    tabs_->setTabText(index, tabLabel.replace(u'&', QStringLiteral("&&")));
    tabs_->setTabToolTip(index, session->path().isEmpty() ? session->label() : session->path());
    if (active_ == session) {
        setWindowTitle(label + QStringLiteral(" — Crumb Writer"));
        updateFooter();
    }
}

void MainWindow::updateFooter()
{
    auto *s = active_;
    if (!s) return;
    const bool hint = !s->synonymHint_.isEmpty() && s->statusMessage_ == s->synonymHint_;
    status_->setPalette(hint ? EditorColors::statusHintPalette(status_->parentWidget()->palette()) : QPalette());
    status_->setText(s->statusMessage_);
    const QString backup = s->store_->backupPath();
    status_->setToolTip(backup.isEmpty() ? s->path() : s->path() + tr("\nStartup backup: ") + backup);
    notice_->setText(s->noticeMessage_);
    noticeBar_->setVisible(!s->noticeMessage_.isEmpty());
    fitFooter();
}
