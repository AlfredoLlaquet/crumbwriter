#include "mainwindow.h"
#include "bookmarks.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"
#include "preferences.h"
#include "searchpanel.h"
#include "spellcheck.h"
#include "synonympanel.h"
#include "textkeys.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#ifdef Q_OS_MACOS
#include <QMenuBar>
#endif
#include <QRegularExpression>
#include <QSplitter>
#include <QStyle>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

void MainWindow::buildToolbar(QVBoxLayout *layout)
{
    auto *bar = new QToolBar("Manuscript tools", this);
    bar->setIconSize(QSize(18, 18));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto tool = [&](const QString &name, const QString &icon, QStyle::StandardPixmap fallback,
                    const QString &tip, auto fn) {
        auto *action = bar->addAction(QIcon::fromTheme(icon, style()->standardIcon(fallback)), name);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, fn);
        return action;
    };
    tool("Open", "document-open", QStyle::SP_DialogOpenButton, "Open a manuscript (Ctrl+O)",
         [this] { chooseFile(false); });
    appendTool_ = tool("New Crumb", "list-add", QStyle::SP_FileIcon, "Append a crumb (Ctrl+N)",
                       [this] { appendCrumb(); });
    tool("Save", "document-save", QStyle::SP_DialogSaveButton, "Save (Ctrl+S)",
         [this] { save(false); });
    tool("Find", "edit-find", QStyle::SP_FileDialogContentsView, "Find forward (Ctrl+F)",
         [this] { search_->find(false); });
    menuButton_ = new QToolButton(bar);
    menuButton_->setIcon(QIcon::fromTheme("application-menu", style()->standardIcon(QStyle::SP_TitleBarMenuButton)));
    menuButton_->setAccessibleName("Document menu");
    menuButton_->setToolTip("Document menu (F10)");
    menuButton_->setPopupMode(QToolButton::InstantPopup);
    bar->addWidget(menuButton_);
    layout->addWidget(bar);
}

void MainWindow::buildMenu()
{
    menu_ = new QMenu("Document", this);
    menuButton_->setMenu(menu_);
    auto add = [this](QMenu *menu, const QString &name, const QString &keys, auto fn) {
        auto *action = menu->addAction(keys.isEmpty() ? name : name + "\t" + keys);
        connect(action, &QAction::triggered, this, fn);
        return action;
    };
    add(menu_, "Open…", "Ctrl+O", [this] { chooseFile(false); });
    add(menu_, "New window", "Ctrl+Shift+N", [this] { if (newRequested) newRequested(); });
    appendAction_ = add(menu_, "New crumb", "Ctrl+N", [this] { appendCrumb(); });
    add(menu_, "Save", "Ctrl+S", [this] { save(false); });
    add(menu_, "Save As…", "Ctrl+Shift+S", [this] { save(true); });
    auto *edit = menu_->addMenu("Edit");
    add(edit, "Undo", "Ctrl+Z", [this] { editor_->undo(); });
    add(edit, "Redo", "Ctrl+Shift+Z", [this] { editor_->redo(); });
    crumbOnlyActions_ << add(edit, "Copy lowercase header", "Ctrl+1", [this] { editor_->copyHeader(); editor_->setFocus(); });
    copyAction_ = add(edit, "Copy crumb", "Ctrl+Q", [this] { editor_->copyCrumb(); editor_->setFocus(); });
    deleteAction_ = add(edit, "Delete current crumb", "Ctrl+Y", [this] { editor_->deleteCrumb(); editor_->setFocus(); });
    add(edit, "Delete word or space", "Ctrl+W", [this] { editor_->deleteWord(); editor_->setFocus(); });
    add(edit, "Next spelling issue…", "F8", [this] { spelling_->next(false); });
    add(edit, "Previous spelling issue…", "Shift+F8", [this] { spelling_->next(true); });
    add(edit, "Manuscript dictionary…", "Ctrl+Shift+D", [this] { spelling_->showDictionary(); });
    auto *nav = menu_->addMenu("Navigate");
#ifdef Q_OS_MACOS
    const QString motionKeys = "Ctrl+Shift+Space";
    const QString bookmarkKeys = "Ctrl+Shift+B";
#else
    const QString motionKeys = "Ctrl+Space";
    const QString bookmarkKeys = "Alt+B";
#endif
    add(nav, "Select by movement", motionKeys, [this] {
        editor_->setFocus();
        editor_->startViSelection();
    });
    add(nav, "Find forward…", "Ctrl+F", [this] { search_->find(false); });
    add(nav, "Find backward…", "Ctrl+G", [this] { search_->find(true); });
    add(nav, "Find plain text…", "Ctrl+P", [this] { search_->findInMode(cwsearch::Mode::Text); });
    add(nav, "Find regular expression…", "Ctrl+R", [this] { search_->findInMode(cwsearch::Mode::Regex); });
    add(nav, "Find and Replace…", "Ctrl+H", [this] { search_->findReplace(); });
    goToNumberAction_ = add(nav, "Go to crumb number…", "Ctrl+L", [this] { search_->goToNumber(); });
    add(nav, "Back", "F2", [this] { goBack(false); });
    add(nav, "Forward", "Shift+F2", [this] { goBack(true); });
    crumbOnlyActions_ << add(nav, "Follow crumb reference", "Ctrl+Shift+J", [this] { followReference(); });
    crumbOnlyActions_ << add(nav, "Next numbering warning", "F4", [this] { numberingWarning(false); });
    crumbOnlyActions_ << add(nav, "Previous numbering warning", "Shift+F4", [this] { numberingWarning(true); });
    add(nav, "Add bookmark…", "Ctrl+B", [this] { bookmarks_->addCurrent(); });
    add(nav, "Bookmarks…", bookmarkKeys, [this] { bookmarks_->showWindow(); });
    add(nav, "Search Characters", "Ctrl+Shift+1", [this] { focusSynonyms(false); });
    add(nav, "Search Recurring", "Ctrl+Shift+2", [this] { focusSynonyms(true); });
    add(nav, "Search visit history", "Ctrl+Shift+3", [this] { focusNavigation(false); });
    indexAction_ = add(nav, "Search genres", "Ctrl+Shift+4", [this] { focusNavigation(true); });
    sortAction_ = add(nav, "Change genre order", "Shift+F7", [this] { side_->toggleGenreOrder(); });
    menu_->addSeparator();
    add(menu_, "Show or hide right panel", "F9", [this] { showSidebar(side_->isHidden()); });
    add(menu_, "Show or hide left panel", "Shift+F9", [this] {
        showPanel(synonyms_, synonyms_->isHidden(), synonymWidth_);
    });
    add(menu_, "Manuscript statistics…", "Ctrl+Shift+I", [this] { showStatistics(); });
    add(menu_, "Settings…", "Ctrl+,", [this] { prefs_.show(this); });
    add(menu_, "Keyboard Shortcuts…", "F1", [this] { showShortcutHelp(this); });
    add(menu_, "&About Crumb Writer…", {}, [this] { showAbout(); });
#ifdef Q_OS_MACOS
    add(menu_, "Close window", "Cmd+W", [this] { close(); });
    // Keep the same F10 menu as the other desktops. Finder users also get
    // a native File menu and the standard application-menu roles.
    auto *bar = new QMenuBar(this);
    bar->setNativeMenuBar(true);
    layout()->setMenuBar(bar);
    auto *file = bar->addMenu("File");
    auto native = [this](QMenu *menu, const QString &name, QKeySequence shortcut,
                         auto fn, QAction::MenuRole role = QAction::NoRole) {
        auto *action = new QAction(name, menu);
        action->setMenuRole(role);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, fn);
        menu->addAction(action);
    };
    // AA_MacDontSwapCtrlAndMeta assigns physical Command to Meta. Bind it
    // explicitly even when a theme supplies PC-style StandardKey defaults.
    native(file, "New Window", Qt::META | Qt::Key_N, [this] { if (newRequested) newRequested(); });
    native(file, "Open…", Qt::META | Qt::Key_O, [this] { chooseFile(false); });
    native(file, "Save", Qt::META | Qt::Key_S, [this] { save(false); });
    native(file, "Save As…", Qt::META | Qt::SHIFT | Qt::Key_S, [this] { save(true); });
    native(file, "Close Window", Qt::META | Qt::Key_W, [this] { close(); });
    native(file, "Settings…", Qt::META | Qt::Key_Comma, [this] { prefs_.show(this); }, QAction::PreferencesRole);
    native(file, "About Crumb Writer", {}, [this] { showAbout(); }, QAction::AboutRole);
    native(file, "Quit Crumb Writer", Qt::META | Qt::Key_Q, [] { QApplication::closeAllWindows(); }, QAction::QuitRole);
    native(bar->addMenu("Navigate"), "Find…", Qt::META | Qt::Key_F, [this] { search_->find(false); });
#else
    add(menu_, "Close window", "Alt+W", [this] { close(); });
#endif
}

void MainWindow::updateModeControls()
{
    const bool generic = editor_->model().isGeneric();
    mode_->setText(generic ? "Generic mode" : "Crumb mode");
    mode_->setToolTip("The first line selects Crumb mode when it contains // outside a URL.");
    appendTool_->setText(generic ? "New Paragraph" : "New Crumb");
    appendTool_->setToolTip(generic ? "Append a paragraph (Ctrl+N)" : "Append a crumb (Ctrl+N)");
    appendAction_->setText(generic ? "New paragraph\tCtrl+N" : "New crumb\tCtrl+N");
    copyAction_->setText(generic ? "Copy paragraph\tCtrl+Q" : "Copy crumb\tCtrl+Q");
    deleteAction_->setText(generic ? "Delete current paragraph\tCtrl+Y" : "Delete current crumb\tCtrl+Y");
    goToNumberAction_->setText(generic ? "Go to line number…\tCtrl+L" : "Go to crumb number…\tCtrl+L");
    indexAction_->setText(generic ? "Search titles\tCtrl+Shift+4" : "Search genres\tCtrl+Shift+4");
    sortAction_->setText(generic ? "Change title order\tShift+F7" : "Change genre order\tShift+F7");
    for (auto *action : crumbOnlyActions_) action->setVisible(!generic);
}

void MainWindow::focusNavigation(bool titlesOrGenres)
{
    showSidebar(true);
    if (titlesOrGenres) side_->focusGenres();
    else side_->focusHistory();
}

void MainWindow::jump(int position)
{
    if (position < 0) return;
    if (position != editor_->textCursor().position()) trail_.remember(editor_->textCursor());
    editor_->jumpTo(position);
    editor_->setFocus();
}

void MainWindow::appendCrumb()
{
    if (!search_->isHidden()) search_->closeSearch();
    editor_->appendCrumb();
    editor_->setFocus();
}

void MainWindow::goBack(bool forward)
{
    auto target = trail_.move(editor_->textCursor(), forward);
    if (!target.isNull()) {
        editor_->jumpTo(target.position());
        editor_->setFocus();
    } else {
        showNotice(forward ? "No later jump." : "No earlier jump.");
    }
}

void MainWindow::numberingWarning(bool backward)
{
    const auto caret = editor_->textCursor();
    auto warn = editor_->model().nextWarning(caret.selectionStart(), backward);
    if (!warn) { showNotice("No numbering warnings in this manuscript."); return; }
    const auto block = editor_->document()->findBlockByNumber(warn->line);
    if (!block.isValid()) return;
    trail_.remember(caret);
    editor_->selectRange(block.position() + warn->end, block.position() + warn->start);
    editor_->setFocus();
    const QString subject = warn->genre.isEmpty() ? "Crumb number" : warn->genre + " genre number";
    showNotice(QString("%1 on line %2. Expected %3, found %4.")
               .arg(subject).arg(warn->line + 1).arg(warn->expected, warn->actual));
}

void MainWindow::followReference()
{
    const QTextCursor c = editor_->textCursor();
    const auto &record = editor_->model().record(c.blockNumber());
    QString labels = "crumb";
    if (!record.label.isEmpty() && record.label.compare("crumb", Qt::CaseInsensitive) != 0)
        labels += "|" + QRegularExpression::escape(record.label);
    const QRegularExpression expression("(?<![\\p{L}\\p{M}\\p{N}_])(?:" + labels +
        ")\\h+([0-9]+)(?![\\p{L}\\p{M}\\p{N}_])",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const int here = c.positionInBlock();
    const QString line = ManuscriptText::blockText(c.block());
    auto matches = expression.globalMatch(line);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (here < match.capturedStart() || here > match.capturedEnd()) continue;
        bool ok;
        const quint64 number = match.captured(1).toULongLong(&ok);
        const int dest = ok ? editor_->model().findNumber(number, c.position(), false, true) : -1;
        if (dest >= 0) jump(dest);
        else showNotice(QString("Crumb %1 was not found.").arg(match.captured(1)));
        return;
    }
    showNotice("Place the caret on a reference such as “crumb 200”.");
}

bool MainWindow::eventFilter(QObject *watched, QEvent *e)
{
    if (e->type() != QEvent::KeyPress) return false;
    auto *key = static_cast<QKeyEvent *>(e);
#ifdef Q_OS_MACOS
    if (watched == menu_ && key->key() == Qt::Key_A && !key->modifiers()) {
        menu_->hide();
        showAbout();
        return true;
    }
#endif
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this) return false;
    if (widget == editor_ && editor_->viSelectionActive()) return false;
    if (isComposedTextKey(*key)) return false;
    const bool inSynonyms = widget == synonyms_ || synonyms_->isAncestorOf(widget);
    const bool inPanel = inSynonyms || widget == side_ || side_->isAncestorOf(widget);
    if (!inPanel && search_->handleKey(key)) return true;
    const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                           Qt::ShiftModifier | Qt::MetaModifier);
    const int code = key->key();
    if (mods == Qt::ControlModifier) {
        switch (code) {
        case Qt::Key_O: chooseFile(false); return true;
        case Qt::Key_S: save(false); return true;
        case Qt::Key_Comma: prefs_.show(this); return true;
        case Qt::Key_B: bookmarks_->addCurrent(); return true;
        case Qt::Key_N: appendCrumb(); return true;
        default: break;
        }
    } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
#ifdef Q_OS_MACOS
        if (code == Qt::Key_B) { bookmarks_->showWindow(); return true; }
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
        if (code == Qt::Key_N) { if (newRequested) newRequested(); return true; }
        if (code == Qt::Key_S) { save(true); return true; }
        if (code == Qt::Key_J && editor_->hasFocus() && !editor_->model().isGeneric()) { followReference(); return true; }
        if (code == Qt::Key_I) { showStatistics(); return true; }
        if (code == Qt::Key_D) { spelling_->showDictionary(); return true; }
    } else if (mods == Qt::AltModifier) {
        switch (code) {
        case Qt::Key_B: bookmarks_->showWindow(); return true;
        case Qt::Key_W: close(); return true;
        default: break;
        }
    } else if (mods == Qt::ShiftModifier) {
        if (code == Qt::Key_F7) { side_->toggleGenreOrder(); return true; }
        if (code == Qt::Key_F9) {
            showPanel(synonyms_, synonyms_->isHidden(), synonymWidth_);
            return true;
        }
    } else if (mods == Qt::NoModifier) {
        switch (code) {
        case Qt::Key_F1: showShortcutHelp(this); return true;
        case Qt::Key_F9: showSidebar(side_->isHidden()); return true;
        case Qt::Key_F10:
            menu_->popup(menuButton_->mapToGlobal(QPoint(0, menuButton_->height())));
            return true;
        default: break;
        }
    }
    if (editor_->hasFocus() && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        if (code == Qt::Key_F2) { goBack(mods == Qt::ShiftModifier); return true; }
        if (code == Qt::Key_F4 && !editor_->model().isGeneric()) { numberingWarning(mods == Qt::ShiftModifier); return true; }
        if (code == Qt::Key_F8) { spelling_->next(mods == Qt::ShiftModifier); return true; }
    }
    if (code == Qt::Key_Escape && mods == Qt::NoModifier && !inPanel) {
        editor_->clearVirtual();
        editor_->clearEditorSelection();
        noticeBar_->hide();
        editor_->setFocus();
        return true;
    }
    return false;
}
