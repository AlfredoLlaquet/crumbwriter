#include "gui-audit.h"
#include "mainwindow.h"
#include "manuscripteditor.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace {
void settle()
{
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void waitFor(const std::function<bool()> &condition, const QString &description)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition()) {
        if (elapsed.elapsed() >= 6000) throw std::runtime_error(description.toStdString());
        settle();
        QThread::msleep(5);
    }
}

template<class T>
T *visible(QObject &owner, const QString &name = {})
{
    T *found = nullptr;
    waitFor([&] {
        for (auto *widget : owner.findChildren<T *>(name)) {
            if (widget->isVisible()) {
                found = widget;
                break;
            }
        }
        return found != nullptr;
    }, QStringLiteral("Expected a visible %1 (%2)").arg(T::staticMetaObject.className(), name));
    return found;
}

void key(QWidget *target, int code, Qt::KeyboardModifiers modifiers = {})
{
    QKeyEvent down(QEvent::KeyPress, code, modifiers);
    QApplication::sendEvent(target, &down);
    QKeyEvent up(QEvent::KeyRelease, code, modifiers);
    QApplication::sendEvent(target, &up);
    settle();
}

void dismiss(QDialog *dialog)
{
    // QFileDialog and QColorDialog can be delegated to the platform. reject()
    // dismisses both the native panel and its Qt owner without changing files.
    dialog->reject();
    settle();
}

QString caption(QString text)
{
    return text.section(u'\t', 0, 0).remove(u'&');
}

QAction *action(QMenu *menu, const QString &text)
{
    for (auto *item : menu->actions())
        if (caption(item->text()) == text) return item;
    throw std::runtime_error(("Missing menu action " + text).toStdString());
}

QAbstractButton *button(QObject &owner, const QString &text)
{
    for (auto *item : owner.findChildren<QAbstractButton *>())
        if (item->isVisible() && caption(item->text()) == text) return item;
    throw std::runtime_error(("Missing dialog button " + text).toStdString());
}

void inspectMenu(GuiAudit &audit, QMenu *menu, QWidget *anchor, const QString &id)
{
    if (!menu) throw std::runtime_error(("Missing popup " + id).toStdString());
    menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
    audit.capture(id, menu);
    menu->hide();
    settle();
}

QMenu *documentMenu(MainWindow &window)
{
    auto *button = window.findChild<QToolButton *>("documentMenuButton");
    if (button && button->menu()) return button->menu();
    throw std::runtime_error("Missing Document menu");
}

void editorFocus(MainWindow &window)
{
    window.activateManuscript(false);
    waitFor([&] { return window.editor()->hasFocus(); }, "Manuscript did not regain keyboard focus");
}

void useLargeManuscriptFont(ManuscriptEditor *editor)
{
    auto font = editor->font();
    font.setPointSize(96);
    editor->setFont(font);
}

void checkControlFont(GuiAudit &audit, const QFont &testedFont, const QFont &controlFont, const QString &name)
{
    const QFontInfo expected(controlFont);
    const QFontInfo actual(testedFont);
    const auto description = [](const QFontInfo &info) {
        return QString("%1, %2 pt, %3 px").arg(info.family()).arg(info.pointSizeF()).arg(info.pixelSize());
    };
    qInfo().noquote() << name << description(actual) << "(reference:" << description(expected) + ')';
    audit.require(actual.family() == expected.family() && actual.pixelSize() == expected.pixelSize(),
                  name + " with a 96-point manuscript: " + description(actual) + "; expected " + description(expected));
}

void auditMenus(GuiAudit &audit, MainWindow &window)
{
    auto *menu = documentMenu(window);
    key(window.editor(), Qt::Key_F10);
    waitFor([menu] { return menu->isVisible(); }, "F10 did not show the Document menu");
    audit.capture("document-menu", menu);
    menu->hide();
    for (const QString &name : {QStringLiteral("Edit"), QStringLiteral("Navigate"), QStringLiteral("View")})
        inspectMenu(audit, action(menu, name)->menu(), &window, "document-menu-" + name.toLower());
#ifdef Q_OS_MACOS
    auto *bar = window.findChild<QMenuBar *>();
    bool file = false, navigation = false, about = false, settings = false, quit = false;
    if (bar && bar->isNativeMenuBar()) {
        for (const auto *top : bar->actions()) {
            const auto *sub = top->menu();
            if (!sub) continue;
            file |= sub->title() == "File";
            navigation |= sub->title() == "Navigate";
            for (const auto *item : sub->actions()) {
                about |= item->menuRole() == QAction::AboutRole;
                settings |= item->menuRole() == QAction::PreferencesRole;
                quit |= item->menuRole() == QAction::QuitRole;
            }
        }
    }
    audit.require(file && navigation && about && settings && quit,
                  "The native macOS menu bar offers File, Navigate and the standard application roles");
#endif
}

void auditSearch(GuiAudit &audit, MainWindow &window)
{
    key(window.editor(), Qt::Key_H, Qt::ControlModifier);
    auto *query = visible<QLineEdit>(window, "cw-find");
    query->setText("moon");
    auto *replacement = visible<QLineEdit>(window, "cw-replacement");
    replacement->setText("planet");
    auto *all = visible<QPushButton>(window, "cw-replace-all");
    waitFor([all] { return all->isEnabled(); }, "Replacement search did not finish");
    all->click();
    visible<QPlainTextEdit>(window, "cw-replace-preview");
    audit.capture("search-and-replace", &window);
    auto *mode = visible<QToolButton>(window, "cw-search-mode");
    inspectMenu(audit, mode->menu(), mode, "search-modes");
    auto *recent = visible<QPushButton>(window, "cw-search-recent");
    recent->click();
    auto *list = visible<QListView>(window, "cw-recent-list");
    audit.require(list->model()->index(0, 0).data().toString().contains("moon"),
                  "The recent-search popup displays the actual query");
    audit.capture("recent-searches", list->window());
    list->window()->hide();
    key(query, Qt::Key_Escape);
}

void auditSettings(GuiAudit &audit, MainWindow &window)
{
    key(window.editor(), Qt::Key_Comma, Qt::ControlModifier);
    auto *settings = visible<QDialog>(window, "preferencesDialog");
    auto *tabs = visible<QTabWidget>(*settings);
    audit.capture("settings-writing", settings);
    tabs->setCurrentIndex(1);
    auto *color = visible<QLineEdit>(*settings, "underline-color");
    audit.capture("settings-names", settings);
    button(*settings, "Choose color…")->click();
    auto *picker = visible<QColorDialog>(*settings, "underlineColorDialog");
    visible<QDialogButtonBox>(*picker);
    visible<QLineEdit>(*picker);
    audit.capture("underline-color", picker);
    dismiss(picker);
    audit.require(color->isVisible(), "The underline color field remains available after canceling the chooser");
    dismiss(settings);
}

void auditInformation(GuiAudit &audit, MainWindow &window)
{
    key(window.editor(), Qt::Key_F1);
    auto *help = visible<QDialog>(window, "shortcutHelp");
    auto *table = visible<QTreeWidget>(*help, "shortcutTable");
    audit.require(table->topLevelItemCount() > 0, "The keyboard help contains shortcuts");
    audit.capture("keyboard-shortcuts", help);
    dismiss(help);

    action(documentMenu(window), "About Crumb Writer…")->trigger();
    auto *about = visible<QDialog>(window, "aboutDialog");
    audit.capture("about", about);
    visible<QPushButton>(*about, "viewLicense")->click();
    auto *license = visible<QDialog>(*about, "licenseDialog");
    audit.capture("license", license);
    dismiss(license);
    dismiss(about);

    auto *title = visible<QLabel>(window, "crumbTitle");
    title->setFocus();
    key(title, Qt::Key_Return);
    auto *fullTitle = visible<QDialog>(window, "fullCrumbTitle");
    visible<QPlainTextEdit>(*fullTitle);
    audit.capture("full-crumb-title", fullTitle);
    dismiss(fullTitle);

    key(window.editor(), Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    auto *stats = visible<QDialog>(window, "statisticsDialog");
    audit.capture("statistics", stats);
    dismiss(stats);

    action(action(documentMenu(window), "View")->menu(), "Arrange Panels and Columns…")->trigger();
    auto *layout = visible<QDialog>(window, "layoutEditor");
    audit.capture("arrange-panels", layout);
    dismiss(layout);
}

QDialog *openBookmarks(MainWindow &window)
{
    action(action(documentMenu(window), "Navigate")->menu(), "Bookmarks…")->trigger();
    return visible<QDialog>(window, "bookmarkWindow");
}

void captureBookmarkList(GuiAudit &audit, QDialog *manager)
{
    visible<QComboBox>(*manager, "bookmarkViewMode")->setCurrentIndex(1);
    auto *tree = visible<QTreeWidget>(*manager, "bookmarkResults");
    auto *filter = visible<QLineEdit>(*manager, "bookmarkFilter");
    filter->setText("Scenes");
    tree->setCurrentItem(tree->topLevelItem(0));
    audit.capture("bookmarks-list", manager);
    filter->clear();
    tree->setCurrentItem(tree->topLevelItem(0));
}

void auditBookmarks(GuiAudit &audit, MainWindow &window)
{
    key(window.editor(), Qt::Key_B, Qt::ControlModifier);
    auto *form = visible<QDialog>(window, "bookmarkEditWindow");
    visible<QComboBox>(*form, "bookmarkCategory")->setEditText("Places");
    audit.capture("bookmark-add", form);
    dismiss(form);

    auto *manager = openBookmarks(window);
    auto *tree = visible<QTreeWidget>(*manager, "bookmarkResults");
    auto *view = visible<QComboBox>(*manager, "bookmarkViewMode");
    view->setCurrentIndex(0);
    auto *group = tree->topLevelItem(0);
    if (!group || !group->childCount()) throw std::runtime_error("Expected a populated bookmark category");
    tree->setCurrentItem(group);
    audit.capture("bookmarks-tree", manager);
    key(tree, Qt::Key_F2);
    form = visible<QDialog>(window, "bookmarkEditWindow");
    audit.capture("bookmark-rename-category", form);
    dismiss(form);

    captureBookmarkList(audit, manager);
    key(tree, Qt::Key_F2);
    form = visible<QDialog>(window, "bookmarkEditWindow");
    audit.capture("bookmark-edit", form);
    dismiss(form);

    key(tree, Qt::Key_Delete);
    auto *question = visible<QMessageBox>(*manager);
    audit.capture("bookmark-delete", question);
    dismiss(question);
    auto *header = tree->header();
    header->customContextMenuRequested(header->rect().center());
    auto *menu = visible<QMenu>(*header);
    audit.capture("column-menu", menu);
    menu->close();
    dismiss(manager);
}

void auditSynonyms(GuiAudit &audit, MainWindow &window, const QString &section)
{
    auto *control = visible<QToolButton>(window, section + "Menu");
    auto *search = visible<QLineEdit>(window, section + "Search");
    key(window.editor(), section == "characters" ? Qt::Key_1 : Qt::Key_2, Qt::ControlModifier | Qt::ShiftModifier);
    waitFor([search] { return search->hasFocus(); }, section + " search shortcut did not focus the field");
    key(search, Qt::Key_Backtab, Qt::ShiftModifier);
    waitFor([control] { return control->hasFocus(); }, "Shift+Tab did not focus the " + section + " menu button");
    key(control, Qt::Key_Space);
    auto *menu = control->menu();
    waitFor([menu] { return menu && menu->isVisible() && menu->activeAction(); }, section + " keyboard menu failed to open");
    audit.capture(section + "-menu", menu);
    key(menu, Qt::Key_Escape);
    auto *list = visible<QListView>(window, section + "List");
    list->setCurrentIndex(list->model()->index(0, 0));
    for (bool add : {true, false}) {
        action(control->menu(), add ? "Add…" : "Edit…")->trigger();
        auto *form = visible<QDialog>(window, section + "EditDialog");
        if (add) visible<QLineEdit>(*form, "synonymText")->setText("Stargazer, the lookout");
        audit.capture(section + (add ? "-add" : "-edit"), form);
        dismiss(form);
    }
    action(control->menu(), "Delete…")->trigger();
    auto *question = visible<QMessageBox>(*control->parentWidget());
    audit.capture(section + "-delete", question);
    dismiss(question);
    if (section == "characters") {
        action(control->menu(), "Find all occurrences")->trigger();
        auto *occurrences = visible<QDialog>(window, "synonymOccurrences");
        auto *matches = visible<QTableView>(*occurrences, "synonymMatches");
        waitFor([matches] { return matches->model()->rowCount() > 0; }, "No occurrence rows in the populated manuscript");
        audit.capture("occurrences", occurrences);
        dismiss(occurrences);
    }
}

void auditSpelling(GuiAudit &audit, MainWindow &window)
{
    auto *editor = window.editor();
    const int typo = editor->manuscriptText().indexOf("mispellled");
    if (typo < 0) throw std::runtime_error("The seeded misspelling is missing");
    editor->jumpTo(typo + 1, false);
    key(editor, Qt::Key_F8);
    auto *review = visible<QDialog>(window, "spellingReview");
    auto *issue = visible<QLabel>(*review, "spellingIssue");
    waitFor([issue] { return !issue->text().isEmpty(); }, "No spelling review issue was found");
    audit.capture("spelling", review);
    visible<QPushButton>(*review, "spellingDictionary")->click();
    auto *dictionary = visible<QDialog>(window, "manuscriptDictionary");
    audit.capture("dictionary", dictionary);
    dismiss(dictionary);
    dismiss(review);

    // The production editor uses QMenu::exec(). Sample the actual stack-owned
    // popup and its spelling submenu inside that event loop, then close it.
    const auto manuscriptFont = editor->font();
    const auto restoreFont = qScopeGuard([&] { editor->setFont(manuscriptFont); });
    useLargeManuscriptFont(editor);
    editorFocus(window);
    editor->jumpTo(typo + 1, false);
    QTimer capture;
    capture.setSingleShot(true);
    bool opened = false;
    QObject::connect(&capture, &QTimer::timeout, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            for (auto *candidate : editor->findChildren<QMenu *>())
                if (candidate->isVisible()) { menu = candidate; break; }
        }
        if (!menu) return;
        opened = true;
        audit.capture("editor-context-menu", menu);
        checkControlFont(audit, menu->font(), documentMenu(window)->font(), "The manuscript context menu");
        QMenu *spelling = nullptr;
        for (auto *item : menu->actions())
            if (item->menu() && item->menu()->title() == "Spelling") spelling = item->menu();
        audit.require(spelling != nullptr, "The context menu at a misspelled word offers spelling suggestions");
        if (spelling) {
            spelling->popup(menu->mapToGlobal(QPoint(menu->width(), 0)));
            audit.capture("spelling-menu", spelling);
            checkControlFont(audit, spelling->font(), documentMenu(window)->font(), "The spelling submenu");
            spelling->hide();
        }
        menu->close();
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        for (auto *menu : editor->findChildren<QMenu *>()) if (menu->isVisible()) menu->close();
    });
    capture.start(150);
    watchdog.start(10000);
    const auto pos = editor->cursorRect().center();
    QContextMenuEvent menuEvent(QContextMenuEvent::Keyboard, pos, editor->viewport()->mapToGlobal(pos));
    QApplication::sendEvent(editor->viewport(), &menuEvent);
    audit.require(opened, "The manuscript context menu was visible");
}

void auditWritingAids(GuiAudit &audit, MainWindow &window)
{
    auto *editor = window.editor();
    if (audit.wantsView("manuscript-settings")) {
        action(documentMenu(window), "Manuscript settings…")->trigger();
        auto *dialog = visible<QDialog>(window, "manuscriptSettingsDialog");
        audit.capture("manuscript-settings", dialog);
        dismiss(dialog);
    }
    if (audit.wantsView("startup-backup")) {
        action(documentMenu(window), "Compare with startup backup…")->trigger();
        auto *dialog = visible<QDialog>(window, "backupComparisonDialog");
        audit.capture("startup-backup", dialog);
        dismiss(dialog);
    }
    if (audit.wantsView("offline-thesaurus")) {
        key(editor, Qt::Key_F7);
        auto *dialog = visible<QDialog>(window, "offlineThesaurus");
        auto *query = visible<QLineEdit>(*dialog, "thesaurusQuery");
        query->setText("doctor");
        key(query, Qt::Key_Return);
        auto *results = visible<QTreeWidget>(*dialog, "thesaurusResults");
        waitFor([results] { return results->topLevelItemCount() > 0; }, "No offline thesaurus results appeared");
        audit.capture("offline-thesaurus", dialog);
        dismiss(dialog);
    }
    if (audit.wantsView("today-count")) {
        action(documentMenu(window), "Set Today’s Count…")->trigger();
        auto *dialog = visible<QDialog>(window, "todayCountDialog");
        visible<QLineEdit>(*dialog, "todayCountValue");
        visible<QPushButton>(*dialog, "resetTodayCount");
        audit.capture("today-count", dialog);
        dismiss(dialog);
    }
}

void auditSymbols(GuiAudit &audit, MainWindow &window)
{
    auto *editor = window.editor();
    key(editor, Qt::Key_U, Qt::ControlModifier);
    auto *picker = visible<QDialog>(window, "symbolPicker");
    visible<QLineEdit>(*picker, "symbolSearch")->setText("arrow");
    auto *grid = visible<QListView>(*picker, "symbolGrid");
    const auto arrow = grid->model()->match(grid->model()->index(0, 0), Qt::UserRole,
                                            QStringLiteral("→"), 1, Qt::MatchExactly);
    if (!arrow.isEmpty()) grid->setCurrentIndex(arrow.first());
    audit.capture("symbols", picker);
    dismiss(picker);
}

void auditNameCompletion(GuiAudit &audit, MainWindow &window)
{
    auto *editor = window.editor();
    const auto manuscriptFont = editor->font();
    const auto restoreFont = qScopeGuard([&] { editor->setFont(manuscriptFont); });
    useLargeManuscriptFont(editor);
    editorFocus(window);
    const int ada = editor->manuscriptText().indexOf("Ada Finch");
    if (ada < 0) throw std::runtime_error("The seeded character is missing from the manuscript");
    editor->jumpTo(ada + 3, false); // After "Ada", with room to complete " Finch".
    key(editor, Qt::Key_F5);
    auto *choices = visible<QListView>(*editor, "synonymChoiceList");
    audit.capture("name-completion", choices->window());
    // Cocoa uses different native font sizes for an embedded list and a
    // popup. Compare the popup with itself after restoring the writing font.
    const QFont largeListFont = choices->font();
    const QFont largeHeadingFont = visible<QLabel>(*choices->window())->font();
    auto *referenceList = visible<QListView>(window, "charactersList");
    const auto rowHeight = [](QListView *list) {
        return list->visualRect(list->model()->index(0, 0)).height();
    };
    const int expectedRow = rowHeight(referenceList), actualRow = rowHeight(choices);
    audit.require(actualRow > 0 && expectedRow > 0 && qAbs(actualRow - expectedRow) <= 2,
                  QString("The completion row is %1 px high at a 96-point manuscript font; "
                          "the Characters list row is %2 px high").arg(actualRow).arg(expectedRow));
    key(choices, Qt::Key_Escape);
    editor->setFont(manuscriptFont);
    editorFocus(window);
    key(editor, Qt::Key_F5);
    choices = visible<QListView>(*editor, "synonymChoiceList");
    checkControlFont(audit, largeListFont, choices->font(), "The name completion list");
    checkControlFont(audit, largeHeadingFont, visible<QLabel>(*choices->window())->font(), "The name completion heading");
    audit.require(rowHeight(choices) == actualRow, "Changing the writing font preserves the completion row height");
    key(choices, Qt::Key_Escape);
}

void auditFiles(GuiAudit &audit, MainWindow &window)
{
    for (bool saving : {false, true}) {
        key(window.editor(), saving ? Qt::Key_S : Qt::Key_O,
            saving ? Qt::ControlModifier | Qt::ShiftModifier : Qt::ControlModifier);
        auto *dialog = visible<QFileDialog>(window);
        visible<QLineEdit>(*dialog, "fileNameEdit");
        visible<QDialogButtonBox>(*dialog);
        audit.capture(saving ? "file-save-as" : "file-open", dialog);
        dismiss(dialog);
        editorFocus(window);
    }
}

void auditReloadWarnings(GuiAudit &audit, MainWindow &window)
{
    // A Save As destination can already contain someone else's companion
    // files. This real conflict leaves all four lists pending; Cancel must
    // preserve their contents in both copies. Only temporary files are used.
    QTemporaryDir temp;
    const QString original = window.path();
    const QString copy = temp.filePath("Copy.txt");
    for (const auto suffix : {".characters", ".recurring", ".dictionary", ".bookmarks"}) {
        QFile conflict(copy + suffix);
        if (!conflict.open(QIODevice::WriteOnly) || conflict.write("Existing destination entry\n") < 0)
            throw std::runtime_error("Could not write the conflicting companion fixtures");
    }
    key(window.editor(), Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *save = visible<QFileDialog>(window);
    save->hide();
    save->selectFile(copy);
    save->show();
    static_cast<QDialog *>(save)->accept();
    waitFor([&] { return window.path() == copy; }, "The conflict fixture could not be saved as a new manuscript");
    try {
        for (const QString &name : {QStringLiteral("characters"), QStringLiteral("recurring")}) {
            auto *tool = visible<QToolButton>(window, name + "Menu");
            action(tool->menu(), "Reload list")->trigger();
            auto *question = visible<QMessageBox>(*tool->parentWidget());
            audit.capture(name + "-reload", question);
            dismiss(question);
        }
        key(window.editor(), Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        auto *dictionary = visible<QDialog>(window, "manuscriptDictionary");
        button(*dictionary, "Reload")->click();
        auto *question = visible<QMessageBox>(*dictionary);
        audit.capture("dictionary-reload", question);
        dismiss(question);
        dismiss(dictionary);

        auto *bookmarks = openBookmarks(window);
        button(*bookmarks, "Reload")->click();
        question = visible<QMessageBox>(*bookmarks);
        audit.capture("bookmark-reload", question);
        dismiss(question);
        dismiss(bookmarks);
    } catch (...) {
        window.loadDocument(original);
        throw;
    }
    if (!window.loadDocument(original)) throw std::runtime_error("Could not restore the conflict fixture");
}

void auditGeneric(GuiAudit &audit, MainWindow &window)
{
    const QString crumbFile = window.path();
    QTemporaryDir temp;
    QFile file(temp.filePath("The Observatory.txt"));
    if (!file.open(QIODevice::WriteOnly)
        || file.write("The Observatory\n\nNight watch\n==========\nA clear night\n-------------\n"
                      "Ada Finch adjusted the focus.\nAt the eyepiece\n~~~~~~~~~~~~~~~\n"
                      "The first star\n..............\nShe wrote «Vega» in the log.") < 0)
        throw std::runtime_error("Could not write the generic manuscript fixture");
    file.close();
    audit.require(!window.loadDocument(temp.filePath("Missing.txt")),
                  "An unreadable manuscript raises a notice");
    auto *notice = visible<QLabel>(window, "notice");
    if (!window.loadDocument(file.fileName())) throw std::runtime_error("Could not open the generic manuscript");
    editorFocus(window);
    audit.require(!notice->isVisible() && notice->text().isEmpty(),
                  "Opening a different manuscript clears the preceding document's notice");
    const auto &model = window.editor()->model();
    audit.require(model.headingLevel(2) == 1 && model.headingLevel(4) == 2 &&
                  model.headingLevel(7) == 3 && model.headingLevel(9) == 4,
                  "All four heading levels are present in the generic manuscript view");
    audit.capture("generic-manuscript", &window);
    key(window.editor(), Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    auto *stats = visible<QDialog>(window, "statisticsDialog");
    audit.capture("generic-statistics", stats);
    dismiss(stats);
    QFile malformed(file.fileName() + ".characters");
    if (!malformed.open(QIODevice::WriteOnly) || malformed.write("\"Unclosed synonym\n") < 0)
        throw std::runtime_error("Could not write the malformed companion fixture");
    malformed.close();
    if (!window.loadDocument(file.fileName())) throw std::runtime_error("Could not load the companion-error fixture");
    editorFocus(window);
    audit.require(notice->isVisible() && notice->text().contains("closing double quote"),
                  "A warning raised by the newly opened manuscript remains visible");
    audit.capture("manuscript-notice", &window);
    if (!window.loadDocument(crumbFile)) throw std::runtime_error("Could not reload the original manuscript");
}

void auditUnsaved(GuiAudit &audit, MainWindow &window)
{
    window.editor()->insertLiteral(" draft");
    audit.require(!window.close(), "A pending change keeps the manuscript open");
    auto *box = visible<QMessageBox>(window, "confirmClose");
    audit.capture("unsaved-changes", box);
    dismiss(box);
    window.editor()->undo();
}
}

void auditDesktopSearches(GuiAudit &audit, MainWindow &window)
{
    for (const QString &id : {QStringLiteral("bookmarks-list"), QStringLiteral("symbols")}) {
        if (!audit.wantsView(id)) continue;
        try {
            editorFocus(window);
            if (id == "bookmarks-list") {
                auto *manager = openBookmarks(window);
                captureBookmarkList(audit, manager);
                dismiss(manager);
            } else {
                auditSymbols(audit, window);
            }
        } catch (const std::exception &error) {
            audit.require(false, id + ": " + QString::fromUtf8(error.what()));
            for (auto *dialog : window.findChildren<QDialog *>())
                if (dialog->isVisible()) dialog->reject();
            settle();
        }
    }
}

void auditWindows(GuiAudit &audit, MainWindow &window)
{
    // Keep independent paths running when one view fails to open. No
    // screenshot may be credited to a stale, hidden or replacement widget.
    const auto inspect = [&](const QStringList &views, const std::function<void()> &scenario) {
        if (std::none_of(views.cbegin(), views.cend(), [&](const QString &view) {
            return audit.wantsView(view);
        })) return;
        try {
            editorFocus(window);
            scenario();
        } catch (const std::exception &e) {
            audit.require(false, views.first() + ": " + QString::fromUtf8(e.what()));
        }
        if (auto *popup = QApplication::activePopupWidget()) popup->hide();
        const auto dialogs = window.findChildren<QDialog *>();
        for (auto *dialog : dialogs) if (dialog->isVisible()) dialog->reject();
        settle();
    };
    inspect({"document-menu", "document-menu-edit", "document-menu-navigate", "document-menu-view", "macos-menu-bar"},
            [&] { auditMenus(audit, window); });
    inspect({"search-and-replace", "search-modes", "recent-searches"}, [&] { auditSearch(audit, window); });
    inspect({"settings-writing", "settings-names", "underline-color"}, [&] { auditSettings(audit, window); });
    inspect({"keyboard-shortcuts", "about", "license", "statistics", "arrange-panels"},
            [&] { auditInformation(audit, window); });
    inspect({"bookmark-add", "bookmarks-tree", "bookmark-rename-category", "bookmarks-list", "bookmark-edit",
             "bookmark-delete", "column-menu"}, [&] { auditBookmarks(audit, window); });
    for (const QString &name : {QStringLiteral("characters"), QStringLiteral("recurring")})
    {
        QStringList views{name + "-menu", name + "-add", name + "-edit", name + "-delete"};
        if (name == "characters") views << "occurrences";
        inspect(views, [&] { auditSynonyms(audit, window, name); });
    }
    inspect({"spelling", "dictionary", "editor-context-menu", "spelling-menu"}, [&] { auditSpelling(audit, window); });
    inspect({"offline-thesaurus", "today-count", "manuscript-settings", "startup-backup"}, [&] { auditWritingAids(audit, window); });
    inspect({"symbols"}, [&] { auditSymbols(audit, window); });
    inspect({"name-completion"}, [&] { auditNameCompletion(audit, window); });
    inspect({"file-open", "file-save-as"}, [&] { auditFiles(audit, window); });
    inspect({"characters-reload", "recurring-reload", "dictionary-reload", "bookmark-reload"},
            [&] { auditReloadWarnings(audit, window); });
    inspect({"generic-manuscript", "generic-statistics", "manuscript-notice"}, [&] { auditGeneric(audit, window); });
    inspect({"unsaved-changes"}, [&] { auditUnsaved(audit, window); });
}
