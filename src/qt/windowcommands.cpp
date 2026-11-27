#include "mainwindow.h"
#include "manuscriptsession.h"
#include "manuscriptoptions.h"
#include <QTabWidget>
#include <QTabBar>
#include "wordtools.h"
#include "bookmarks.h"
#include "commandicons.h"
#include "manuscripteditor.h"
#include "manuscriptlinks.h"
#include "manuscripttext.h"
#include "preferences.h"
#include "searchpanel.h"
#include "spellcheck.h"
#include "synonympanel.h"
#include "synonymwriting.h"
#include "textkeys.h"
#include "windowlayout.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMenuBar>
#include <QShortcut>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

void MainWindow::buildToolbar(QVBoxLayout *layout)
{
    using CommandIcons::Symbol;
    auto *bar = new QToolBar("Manuscript tools", this);
    bar->setObjectName("manuscriptToolbar");
    bar->setIconSize(QSize(18, 18));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto tool = [&](const QString &name, Symbol symbol, const QString &tip, auto fn) {
        auto *action = bar->addAction(CommandIcons::icon(symbol, bar), name);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, fn);
        return action;
    };
    tool("Open", Symbol::Open, "Open manuscripts in tabs (Ctrl+O)",
         [this] { active_->chooseFile(false); });
    appendTool_ = tool("New Crumb", Symbol::Add, "Append a crumb (Ctrl+N)",
                       [this] { active_->appendCrumb(); });
    tool("Save", Symbol::Save, "Save (Ctrl+S)",
         [this] { active_->save(false); });
    tool("Find", Symbol::Find, "Find forward (Ctrl+F)",
         [this] { active_->search_->find(false); });
    buildFormattingTools(bar);
    layout->addWidget(bar);
}

void MainWindow::buildMenu()
{
    menuBar_ = new QMenuBar(this);
    menuBar_->setObjectName("manuscriptMenuBar");
    layout()->setMenuBar(menuBar_);
    fileMenu_ = menuBar_->addMenu(tr("&File"));
    auto *edit = menuBar_->addMenu(tr("&Edit"));
    auto *view = menuBar_->addMenu(tr("&View"));
    auto *format = menuBar_->addMenu(tr("F&ormat"));
    auto *nav = menuBar_->addMenu(tr("&Navigate"));
    auto *tools = menuBar_->addMenu(tr("&Tools"));
    auto *tabMenu = menuBar_->addMenu(tr("W&indow"));
    auto *help = menuBar_->addMenu(tr("&Help"));
    auto add = [this](QMenu *menu, const QString &name, const QString &keys, auto fn) {
        auto *action = menu->addAction(keys.isEmpty() ? name : name + "\t" + keys);
        connect(action, &QAction::triggered, this, fn);
        return action;
    };
    add(fileMenu_, "Open…", "Ctrl+O", [this] { active_->chooseFile(false); });
    add(tabMenu, "New tab", "Ctrl+T", [this] { newTab(); });
    add(tabMenu, "New window", "Ctrl+Shift+N", [this] { newWindow()->activateManuscript(false); });
    add(tabMenu, tr("Next tab"), "Ctrl+Tab", [this] { stepTab(1); });
    add(tabMenu, tr("Previous tab"), "Ctrl+Shift+Tab", [this] { stepTab(-1); });
    add(tabMenu, tr("Move tab to new window"), {}, [this] { moveTabToNewWindow(); });
    add(tabMenu, tr("Close tab"), "Ctrl+F4", [this] { closeCurrentTab(); });
    reopenAction_ = add(tabMenu, tr("Reopen closed tab"), "Ctrl+Shift+T", [this] { reopenClosedTab(); });
    reopenAction_->setEnabled(false);
    appendAction_ = add(fileMenu_, "New crumb", "Ctrl+N", [this] { active_->appendCrumb(); });
    add(fileMenu_, "Save", "Ctrl+S", [this] { active_->save(false); });
    add(fileMenu_, "Save As…", "Ctrl+Shift+S", [this] { active_->save(true); });
    add(edit, "Undo", "Ctrl+Z", [this] { active_->editor_->undo(); });
    add(edit, "Redo", "Ctrl+Shift+Z", [this] { active_->editor_->redo(); });
    crumbOnlyActions_ << add(edit, "Copy lowercase header", "Ctrl+1", [this] { active_->editor_->copyHeader(); active_->editor_->setFocus(); });
    copyAction_ = add(edit, "Copy crumb", "Ctrl+Q", [this] { active_->editor_->copyCrumb(); active_->editor_->setFocus(); });
    deleteAction_ = add(edit, "Delete current crumb", "Ctrl+Y", [this] { active_->editor_->deleteCrumb(); active_->editor_->setFocus(); });
    add(edit, "Delete word or space", "Ctrl+W", [this] { active_->editor_->deleteWord(); active_->editor_->setFocus(); });
    add(edit, "Select word at caret", "Ctrl+2", [this] { active_->words_->selectWord(); });
    add(tools, tr("Correct Word at Caret"), "Ctrl+.", [this] { active_->spelling_->correctAtCaret(); });
    add(tools, "Offline Thesaurus…", "F7", [this] { active_->words_->showThesaurus(); });
    genericOnlyActions_ << add(edit, tr("Continue list"), "Ctrl+Shift+Enter", [this] { active_->editor_->continueList(); });
    genericOnlyActions_ << add(edit, tr("Insert guillemet or toggle italics"), "Ctrl+K", [this] { active_->editor_->toggleGuillemets(); });
    auto *web = tools->addMenu("Look Up in Merriam-Webster");
    add(web, "Dictionary", "Ctrl+Shift+L, D", [this] { active_->words_->openWeb(WordTools::Reference::Dictionary); });
    add(web, "Dictionary synonyms", "Ctrl+Shift+L, S", [this] { active_->words_->openWeb(WordTools::Reference::Synonyms); });
    add(web, "Thesaurus", "Ctrl+Shift+L, T", [this] { active_->words_->openWeb(WordTools::Reference::Thesaurus); });
    add(edit, "Insert Symbol…", "Ctrl+U", [this] { active_->openSymbols(); });
    add(edit, "Complete Name", "F5", [this] { active_->editor_->setFocus(); active_->synonymWriting_->complete(); });
    add(tools, "Next spelling issue…", "F8", [this] { active_->spelling_->next(false); });
    add(tools, "Previous spelling issue…", "Shift+F8", [this] { active_->spelling_->next(true); });
    add(tools, "Manuscript dictionary…", "Ctrl+Shift+D", [this] { active_->spelling_->showDictionary(); });
#ifdef Q_OS_MACOS
    const QString motionKeys = "Ctrl+Shift+Space";
    const QString bookmarkKeys = "Ctrl+Shift+B";
#else
    const QString motionKeys = "Ctrl+Space";
    const QString bookmarkKeys = "Alt+B";
#endif
    add(nav, "Select by movement", motionKeys, [this] {
        active_->editor_->setFocus();
        active_->editor_->startViSelection();
    });
    add(nav, "Find forward…", "Ctrl+F", [this] { active_->search_->find(false); });
    add(nav, "Find backward…", "Ctrl+G", [this] { active_->search_->find(true); });
    add(nav, "Find plain text…", "Ctrl+P", [this] { active_->search_->findInMode(cwsearch::Mode::Text); });
    add(nav, "Find regular expression…", "Ctrl+R", [this] { active_->search_->findInMode(cwsearch::Mode::Regex); });
    add(nav, "Find and Replace…", "Ctrl+H", [this] { active_->search_->findReplace(); });
    goToNumberAction_ = add(nav, "Go to crumb number…", "Ctrl+L", [this] { active_->search_->goToNumber(); });
    add(nav, "Back", "F2 / Alt+Left", [this] { active_->goBack(false); });
    add(nav, "Forward", "Shift+F2 / Alt+Right", [this] { active_->goBack(true); });
    add(nav, "Follow Link, Reference, or Mention", "Ctrl+Shift+J", [this] {
        active_->followAt(active_->editor_->textCursor().position(), true);
    });
    add(nav, "Previous Mention", "F6", [this] { active_->synonymWriting_->navigate(true); });
    add(nav, "Next Mention", "Shift+F6", [this] { active_->synonymWriting_->navigate(false); });
    crumbOnlyActions_ << add(nav, "Next numbering warning", "F4", [this] { active_->numberingWarning(false); });
    crumbOnlyActions_ << add(nav, "Previous numbering warning", "Shift+F4", [this] { active_->numberingWarning(true); });
    add(nav, "Add bookmark…", "Ctrl+B", [this] { active_->bookmarks_->addCurrent(); });
    add(nav, "Bookmarks…", bookmarkKeys, [this] { active_->bookmarks_->showWindow(); });
    add(nav, "Search Characters", "Ctrl+Shift+1", [this] { focusSynonyms(false); });
    add(nav, "Search Recurring", "Ctrl+Shift+2", [this] { focusSynonyms(true); });
    add(nav, "Search visit history", "Ctrl+Shift+3", [this] { focusNavigation(false); });
    indexAction_ = add(nav, "Search genres", "Ctrl+Shift+4", [this] { focusNavigation(true); });
    sortAction_ = add(nav, "Change genre order", "Shift+F7", [this] { active_->side_->toggleGenreOrder(); });
    add(view, "Show or Hide Navigation", "F9", [this] { showSidebar(rightPane_->isHidden()); });
    add(view, "Show or Hide Synonym Lists", "Shift+F9", [this] {
        showPanel(leftPane_, leftPane_->isHidden(), synonymWidth_);
    });
    markupAction_ = add(view, tr("Show manuscript markup"), "Ctrl+Shift+F9", [this] {
        active_->editor_->setShowManuscriptMarkup(markupAction_->isChecked());
    });
    markupAction_->setCheckable(true);
    genericOnlyActions_ << markupAction_;
    add(view, "Arrange Panels and Columns…", "Ctrl+Shift+F10", [this] { layouts_->arrange(this); });
    add(view, "Restore Default Layouts", {}, [this] { layouts_->restoreDefaults(); });
    add(tools, tr("Manuscript settings…"), {}, [this] { active_->options_->showSettings(); });
    add(tools, tr("Compare with startup backup…"), {}, [this] { active_->options_->showBackupComparison(); });
    add(tools, "Manuscript statistics…", "Ctrl+Shift+I", [this] { active_->showStatistics(); });
    add(tools, "Set Today’s Count…", {}, [this] { active_->side_->showTodayCountDialog(); });
    auto *settings = add(tools, "Settings…", "Ctrl+,", [this] { prefs_.show(this); });
    settings->setMenuRole(QAction::PreferencesRole);
#ifdef Q_OS_MACOS
    settings->setShortcut(QKeySequence(Qt::META | Qt::Key_Comma));
#endif
    add(help, "Keyboard Shortcuts…", "F1", [this] { showShortcutHelp(this); });
    add(help, "Open TheCrumbs.org", {}, [this] { openCrumbsWebsite(); });
    add(help, "&About Crumb Writer…", {}, [this] { showAbout(); })->setMenuRole(QAction::AboutRole);
    add(fileMenu_, tr("Import…"), {}, [this] { importDocument(); });
    add(fileMenu_, tr("Export…"), {}, [this] { exportDocument(); });
    add(help, tr("Crumb Writer Help"), {}, [this] { showHelp(); });
    add(help, tr("Licenses…"), {}, [this] { showHelp(true); });
    add(tabMenu, tr("Close window"), "Alt+W", [this] { close(); });
    auto *quit = add(fileMenu_, tr("Quit Crumb Writer"), {}, [this] { if (quitApplication) quitApplication(); else QApplication::closeAllWindows(); });
    quit->setMenuRole(QAction::QuitRole);
    charactersAction_ = add(view, tr("Characters"), {}, [this] {
        const bool generic = editor()->model().isGeneric();
        prefs_.settings().setValue(generic ? "panels/generic-characters" : "panels/crumb-characters", charactersAction_->isChecked());
        if (prefs_.changed) prefs_.changed();
        updateModeControls();
    });
    charactersAction_->setCheckable(true);
    auto *styles = format->addMenu(tr("Style"));
    for (int value = -1; value <= 4; ++value) {
        const QString name = value < 0 ? tr("Main Title") : value == 0 ? tr("Regular Text") : tr("Title %1").arg(value);
        auto *action = styles->addAction(name);
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K, %1").arg(value < 0 ? QStringLiteral("M") : QString::number(value))));
        connect(action, &QAction::triggered, this, [this, value] { applyLineStyle(value); });
        styleActions_.append(action);
    }
#ifdef Q_OS_MACOS
    // Physical Command shortcuts reach the same actions as the menu and toolbar.
    auto command = [this](int key, auto fn) {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated, this, fn);
    };
    command(Qt::META | Qt::Key_T, [this] { newTab(); });
    command(Qt::META | Qt::SHIFT | Qt::Key_T, [this] { reopenClosedTab(); });
    command(Qt::META | Qt::Key_N, [this] { newWindow()->activateManuscript(false); });
    command(Qt::META | Qt::Key_O, [this] { active_->chooseFile(false); });
    command(Qt::META | Qt::Key_S, [this] { active_->save(false); });
    command(Qt::META | Qt::SHIFT | Qt::Key_S, [this] { active_->save(true); });
    command(Qt::META | Qt::Key_W, [this] { close(); });
    command(Qt::META | Qt::Key_F, [this] { active_->search_->find(false); });
    quit->setShortcut(QKeySequence(Qt::META | Qt::Key_Q));
#endif
}

void MainWindow::updateModeControls()
{
    const bool generic = active_->editor_->model().isGeneric();
    const bool visible = prefs_.settings().value(generic ? "panels/generic-characters" : "panels/crumb-characters", !generic).toBool();
    charactersAction_->setChecked(visible);
    active_->synonyms_->showCharacters(visible);
    syncFormattingTools();
    mode_->setText(generic ? "Generic mode" : "Crumb mode");
    mode_->setToolTip("The first line selects Crumb mode when it contains // outside a URL.");
    fitFooter();
    appendTool_->setText(generic ? "New Paragraph" : "New Crumb");
    appendTool_->setToolTip(generic ? "Append a paragraph (Ctrl+N)" : "Append a crumb (Ctrl+N)");
    appendAction_->setText(generic ? "New paragraph\tCtrl+N" : "New crumb\tCtrl+N");
    copyAction_->setText(generic ? "Copy paragraph\tCtrl+Q" : "Copy crumb\tCtrl+Q");
    deleteAction_->setText(generic ? "Delete current paragraph\tCtrl+Y" : "Delete current crumb\tCtrl+Y");
    goToNumberAction_->setText(generic ? "Go to line number…\tCtrl+L" : "Go to crumb number…\tCtrl+L");
    indexAction_->setText(generic ? "Search titles\tCtrl+Shift+4" : "Search genres\tCtrl+Shift+4");
    sortAction_->setText(generic ? "Change title order\tShift+F7" : "Change genre order\tShift+F7");
    markupAction_->setChecked(active_->editor_->showManuscriptMarkup());
    for (auto *action : genericOnlyActions_) action->setVisible(generic);
    for (auto *action : crumbOnlyActions_) action->setVisible(!generic);
}

void MainWindow::focusNavigation(bool titlesOrGenres)
{
    showSidebar(true);
    if (titlesOrGenres) active_->side_->focusGenres();
    else active_->side_->focusHistory();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *e)
{
    if (!active_) return false;
    if (watched == tabs_ && e->type() == QEvent::MouseButtonDblClick) {
        const auto *mouse = static_cast<QMouseEvent *>(e);
        if (mouse->button() == Qt::LeftButton && mouse->position().y() <= tabs_->tabBar()->geometry().bottom()) {
            newTab();
            return true;
        }
    }
    if (e->type() == QEvent::WindowActivate) {
        auto *dialog = qobject_cast<QDialog *>(watched);
        if (dialog && dialog->isWindow()) {
            if (auto *session = sessionForDialog(dialog); session && session != active_)
                selectSession(session, false);
        }
    }
    if (watched == qApp && e->type() == QEvent::ApplicationPaletteChange)
        applyPreferences();
    if (e->type() == QEvent::MouseButtonPress && watched == active_->editor_->viewport()) {
        const auto *click = static_cast<QMouseEvent *>(e);
        const auto mods = click->modifiers() & ~Qt::ShiftModifier;
        bool follow = mods == Qt::ControlModifier;
#ifdef Q_OS_MACOS
        follow |= mods == Qt::MetaModifier;
#endif
        if (click->button() == Qt::LeftButton && follow) {
            const int position = active_->editor_->characterUnderPointer(click->position().toPoint());
            if (active_->followAt(position, !click->modifiers().testFlag(Qt::ShiftModifier), true)) return true;
        }
    }
    if (active_->webLookupPending_ && watched == active_->editor_ && e->type() == QEvent::FocusOut) {
        active_->webLookupPending_ = false;
        active_->changed();
    }
    if (e->type() != QEvent::KeyPress) return false;
    auto *key = static_cast<QKeyEvent *>(e);

    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this) return false;
    const auto keys = key->modifiers() & ~Qt::KeypadModifier;
    if (keys == Qt::ControlModifier && key->key() == Qt::Key_Tab) { stepTab(1); return true; }
    if (keys == (Qt::ControlModifier | Qt::ShiftModifier) &&
        (key->key() == Qt::Key_Backtab || key->key() == Qt::Key_Tab)) { stepTab(-1); return true; }
    if (keys == Qt::ControlModifier && key->key() == Qt::Key_T) { newTab(); return true; }
    if (keys == (Qt::ControlModifier | Qt::ShiftModifier) && key->key() == Qt::Key_T) { reopenClosedTab(); return true; }
    if (keys == Qt::ControlModifier && key->key() == Qt::Key_F4) { closeCurrentTab(); return true; }
    if (widget == active_->editor_ && active_->editor_->viSelectionActive()) return false;
    if (widget == active_->editor_ && active_->webLookupPending_) {
        if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift) return true;
        active_->webLookupPending_ = false;
        active_->changed();
        switch (key->key()) {
        case Qt::Key_D: active_->words_->openWeb(WordTools::Reference::Dictionary); break;
        case Qt::Key_S: active_->words_->openWeb(WordTools::Reference::Synonyms); break;
        case Qt::Key_T: active_->words_->openWeb(WordTools::Reference::Thesaurus); break;
        case Qt::Key_Escape: break;
        default: active_->showNavigationStatus("Lookup canceled. Use D, S, or T after Ctrl+Shift+L."); break;
        }
        return true;
    }
    if (isComposedTextKey(*key)) return false;
    if (widget == active_->editor_ && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)
        && key->key() == Qt::Key_L) {
        active_->webLookupPending_ = true;
        active_->setStatusMessage("Merriam-Webster — D Dictionary · S Synonyms · T Thesaurus · Esc Cancel");
        return true;
    }
    const bool inSynonyms = widget == active_->synonyms_ || active_->synonyms_->isAncestorOf(widget);
    const bool inPanel = inSynonyms || widget == active_->side_ || active_->side_->isAncestorOf(widget);
    if (widget == active_->editor_ && key->key() == Qt::Key_Escape && !key->modifiers())
        active_->synonymWriting_->dismissHint();
    if (!inPanel && active_->search_->handleKey(key)) return true;
    const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                           Qt::ShiftModifier | Qt::MetaModifier);
    const int code = key->key();
    if (mods == Qt::ControlModifier) {
        switch (code) {
        case Qt::Key_2: if (widget == active_->editor_) { active_->words_->selectWord(); return true; } break;
        case Qt::Key_O: active_->chooseFile(false); return true;
        case Qt::Key_S: active_->save(false); return true;
        case Qt::Key_Period: if (widget == editor()) { active_->spelling_->correctAtCaret(); return true; } break;
        case Qt::Key_Comma: prefs_.show(this); return true;
        case Qt::Key_B: active_->bookmarks_->addCurrent(); return true;
        case Qt::Key_N: active_->appendCrumb(); return true;
        case Qt::Key_U: active_->openSymbols(); return true;
        default: break;
        }
    } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
#ifdef Q_OS_MACOS
        if (code == Qt::Key_B) { active_->bookmarks_->showWindow(); return true; }
#endif
        // Qt can deliver either the digit or its shifted symbol (e.g. Spanish ·).
        switch (code) {
        case Qt::Key_1: case Qt::Key_Exclam: focusSynonyms(false); return true;
        case Qt::Key_2: case Qt::Key_At: case Qt::Key_QuoteDbl: focusSynonyms(true); return true;
        case Qt::Key_3: case Qt::Key_NumberSign: case Qt::Key_periodcentered:
        case Qt::Key_sterling: focusNavigation(false); return true;
        case Qt::Key_4: case Qt::Key_Dollar: focusNavigation(true); return true;
        default: break;
        }
        if (code == Qt::Key_F9 && active_->editor_->model().isGeneric()) { markupAction_->trigger(); return true; }
        if (code == Qt::Key_N) { newWindow()->activateManuscript(false); return true; }
        if (code == Qt::Key_S) { active_->save(true); return true; }
        if (code == Qt::Key_J && active_->editor_->hasFocus()) { active_->followAt(active_->editor_->textCursor().position(), true); return true; }
        if (code == Qt::Key_I) { active_->showStatistics(); return true; }
        if (code == Qt::Key_D) { active_->spelling_->showDictionary(); return true; }
    } else if (mods == Qt::AltModifier) {
        switch (code) {
        case Qt::Key_B: active_->bookmarks_->showWindow(); return true;
        case Qt::Key_W: close(); return true;
        case Qt::Key_Left:  if (active_->editor_->hasFocus()) { active_->goBack(false); return true; } break;
        case Qt::Key_Right: if (active_->editor_->hasFocus()) { active_->goBack(true); return true; } break;
        default: break;
        }
    } else if (mods == Qt::ShiftModifier) {
        if (code == Qt::Key_F7) { active_->side_->toggleGenreOrder(); return true; }
        if (code == Qt::Key_F9) {
            showPanel(leftPane_, leftPane_->isHidden(), synonymWidth_);
            return true;
        }
    } else if (mods == Qt::NoModifier) {
        switch (code) {
        case Qt::Key_F1: showShortcutHelp(this); return true;
        case Qt::Key_F9: showSidebar(rightPane_->isHidden()); return true;
        case Qt::Key_F10: {
            menuBar_->setFocus();
            menuBar_->setActiveAction(fileMenu_->menuAction());
            fileMenu_->popup(menuBar_->mapToGlobal(menuBar_->actionGeometry(fileMenu_->menuAction()).bottomLeft()));
            return true;
        }
        default: break;
        }
    }
    if (active_->editor_->hasFocus() && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        if (code == Qt::Key_F2) { active_->goBack(mods == Qt::ShiftModifier); return true; }
        if (code == Qt::Key_F7 && !mods) { active_->words_->showThesaurus(); return true; }
        if (code == Qt::Key_F5 && !mods) { active_->synonymWriting_->complete(); return true; }
        if (code == Qt::Key_F6) { active_->synonymWriting_->navigate(mods == Qt::NoModifier); return true; }
        if (code == Qt::Key_F4 && !active_->editor_->model().isGeneric()) { active_->numberingWarning(mods == Qt::ShiftModifier); return true; }
        if (code == Qt::Key_F8) { active_->spelling_->next(mods == Qt::ShiftModifier); return true; }
    }
    if (code == Qt::Key_Escape && mods == Qt::NoModifier && !inPanel) {
        active_->editor_->clearVirtual();
        active_->editor_->clearEditorSelection();
        showNotice({});
        active_->editor_->setFocus();
        return true;
    }
    return false;
}
