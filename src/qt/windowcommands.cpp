#include "mainwindow.h"
#include "bookmarks.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"
#include "preferences.h"
#include "searchpanel.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
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
    };
    tool("Open", "document-open", QStyle::SP_DialogOpenButton, "Open a manuscript (Ctrl+O)",
         [this] { chooseFile(false); });
    tool("New Crumb", "list-add", QStyle::SP_FileIcon, "Append a crumb (Ctrl+N)",
         [this] { editor_->appendCrumb(); editor_->setFocus(); });
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
    add(menu_, "New crumb", "Ctrl+N", [this] { editor_->appendCrumb(); editor_->setFocus(); });
    add(menu_, "Save", "Ctrl+S", [this] { save(false); });
    add(menu_, "Save As…", "Ctrl+Shift+S", [this] { save(true); });
    auto *edit = menu_->addMenu("Edit");
    add(edit, "Undo", "Ctrl+Z", [this] { editor_->undo(); });
    add(edit, "Redo", "Ctrl+Shift+Z", [this] { editor_->redo(); });
    add(edit, "Copy header", "Ctrl+1", [this] { editor_->copyHeader(); editor_->setFocus(); });
    add(edit, "Copy crumb", "Ctrl+Q", [this] { editor_->copyCrumb(); editor_->setFocus(); });
    add(edit, "Delete current crumb", "Ctrl+Y", [this] { editor_->deleteCrumb(); editor_->setFocus(); });
    auto *nav = menu_->addMenu("Navigate");
    add(nav, "Find forward…", "Ctrl+F", [this] { search_->find(false); });
    add(nav, "Find backward…", "Ctrl+G", [this] { search_->find(true); });
    add(nav, "Find and Replace…", "Ctrl+H", [this] { search_->findReplace(); });
    add(nav, "Go to crumb number…", "Ctrl+L", [this] { search_->goToNumber(); });
    add(nav, "Back", "F2", [this] { goBack(false); });
    add(nav, "Forward", "Shift+F2", [this] { goBack(true); });
    add(nav, "Follow crumb reference", "Ctrl+Shift+J", [this] { followReference(); });
    add(nav, "Next numbering warning", "F4", [this] { numberingWarning(false); });
    add(nav, "Previous numbering warning", "Shift+F4", [this] { numberingWarning(true); });
    add(nav, "Add bookmark…", "Ctrl+B", [this] { bookmarks_->addCurrent(); });
    add(nav, "Bookmarks…", "Alt+B", [this] { bookmarks_->showWindow(); });
    menu_->addSeparator();
    add(menu_, "Show or hide sidebar", "F9", [this] { showSidebar(side_->isHidden()); });
    add(menu_, "Settings…", "Ctrl+,", [this] { prefs_.show(this); });
    add(menu_, "Keyboard Shortcuts…", "F1", [this] { showShortcutHelp(this); });
    add(menu_, "Close window", "Ctrl+W", [this] { close(); });
}

void MainWindow::showSidebar(bool show)
{
    if (show) {
        const bool wasHidden = side_->isHidden();
        side_->show();
        if (wasHidden) split_->setSizes({qMax(1, split_->width() - sidebarWidth_), sidebarWidth_});
    } else {
        sidebarWidth_ = qMax(1, side_->width());
        side_->hide();
        editor_->setFocus();
    }
}

void MainWindow::jump(int position)
{
    if (position < 0) return;
    if (position != editor_->textCursor().position()) trail_.remember(editor_->textCursor());
    editor_->jumpTo(position);
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
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this) return false;
    auto *key = static_cast<QKeyEvent *>(e);
    if (search_->handleKey(key)) return true;
    const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                                           Qt::ShiftModifier | Qt::MetaModifier);
    const int code = key->key();
    if (mods == Qt::ControlModifier) {
        switch (code) {
        case Qt::Key_O: chooseFile(false); return true;
        case Qt::Key_S: save(false); return true;
        case Qt::Key_W: close(); return true;
        case Qt::Key_Comma: prefs_.show(this); return true;
        case Qt::Key_B: bookmarks_->addCurrent(); return true;
        case Qt::Key_N: editor_->appendCrumb(); editor_->setFocus(); return true;
        default: break;
        }
    } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (code == Qt::Key_N) { if (newRequested) newRequested(); return true; }
        if (code == Qt::Key_S) { save(true); return true; }
        if (code == Qt::Key_J && editor_->hasFocus()) { followReference(); return true; }
    } else if (mods == Qt::AltModifier && code == Qt::Key_B) {
        bookmarks_->showWindow();
        return true;
    } else if (mods == Qt::NoModifier) {
        switch (code) {
        case Qt::Key_F1: showShortcutHelp(this); return true;
        case Qt::Key_F6:
            if (editor_->hasFocus()) { showSidebar(true); side_->focusHistory(); }
            else editor_->setFocus();
            return true;
        case Qt::Key_F7: showSidebar(true); side_->focusGenres(); return true;
        case Qt::Key_F9: showSidebar(side_->isHidden()); return true;
        case Qt::Key_F10:
            menu_->popup(menuButton_->mapToGlobal(QPoint(0, menuButton_->height())));
            return true;
        default: break;
        }
    }
    if (editor_->hasFocus() && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        if (code == Qt::Key_F2) { goBack(mods == Qt::ShiftModifier); return true; }
        if (code == Qt::Key_F4) { numberingWarning(mods == Qt::ShiftModifier); return true; }
    }
    if (code == Qt::Key_Escape && mods == Qt::NoModifier) {
        editor_->clearVirtual();
        editor_->clearEditorSelection();
        noticeBar_->hide();
        editor_->setFocus();
        return true;
    }
    return false;
}
