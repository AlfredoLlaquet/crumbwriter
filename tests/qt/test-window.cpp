#include "mainwindow.h"
#include "documentstore.h"
#include "manuscriptpath.h"
#include "buildinfo.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "statisticsdialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMenuBar>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextLayout>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
void require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "Window regression at line %d: %s\n", line, expression);
        std::exit(1);
    }
}
#define CHECK(value) require(bool(value), #value, __LINE__)

bool until(const std::function<bool()> &done)
{
    QElapsedTimer clock;
    clock.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        if (done()) return true;
        QThread::msleep(2);
    } while (clock.elapsed() < 5000);
    return false;
}

void sendKey(int sourceLine, int code, Qt::KeyboardModifiers mods = {}, const QString &text = {})
{
    require(until([] { return QApplication::focusWidget() != nullptr; }), "a widget has keyboard focus", sourceLine);
    auto *target = QApplication::focusWidget();
    QKeyEvent press(QEvent::KeyPress, code, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, code, mods, text);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

#define key(...) sendKey(__LINE__, __VA_ARGS__)

void write(const QString &path, const QByteArray &bytes)
{
    QFile out(path);
    CHECK(out.open(QIODevice::WriteOnly));
    CHECK(out.write(bytes) == bytes.size());
}

std::optional<QByteArray> readIfAvailable(const QString &path, QString *error = nullptr)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        if (file.error() == QFileDevice::NoError) return data;
    }
    if (error) *error = file.errorString();
    return std::nullopt;
}

QByteArray readAt(const QString &path, int line)
{
    QString error;
    auto bytes = readIfAvailable(path, &error);
    if (!bytes) std::fprintf(stderr, "Cannot read %s at test line %d: %s\n",
                             qPrintable(path), line, qPrintable(error));
    require(bytes.has_value(), "read the published file", line);
    return *bytes;
}
#define read(path) readAt(path, __LINE__)

bool published(ManuscriptEditor *editor)
{
    const auto *tabs = editor->window()->findChild<QTabWidget *>("manuscript");
    return !tabs->tabText(tabs->indexOf(editor)).startsWith(QStringLiteral("• "));
}

void pressChoice(MainWindow &window, const QString &choice)
{
    auto *dialog = window.findChild<QMessageBox *>("confirmClose");
    CHECK(dialog && dialog->isVisible());
    for (auto *button : dialog->buttons()) {
        if (button->text() == choice) {
            button->click();
            QApplication::processEvents();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return;
        }
    }
    CHECK(false);
}

void testManuscriptPaths(const QTemporaryDir &tmp)
{
    const QDir files(tmp.filePath("Path fixtures"));
    CHECK(QDir().mkpath(files.absolutePath()));
    const QString sourceA = files.filePath("Original A.txt"), sourceB = files.filePath("Original B.txt");
    const QString missingA = files.filePath("Missing A.txt"), missingB = files.filePath("Missing B.txt");
    const QString newA = files.filePath("New A.txt"), newB = files.filePath("New B.txt");
    write(sourceA, "Original A.");
    write(sourceB, "Original B.");
    CHECK(!ManuscriptPath::same(missingA, missingB));
    CHECK(ManuscriptPath::same(missingA, files.path() + "/./Missing A.txt"));
    CHECK(!ManuscriptPath::same(sourceA, sourceB));

    namespace fs = std::filesystem;
    const auto native = [](const QString &name) { return QFileInfo(name).filesystemAbsoluteFilePath(); };
    const QString hardLink = tmp.filePath("Hard link.txt");
    std::error_code error;
    fs::create_hard_link(native(sourceA), native(hardLink), error);
    CHECK(!error && ManuscriptPath::same(sourceA, hardLink));
    const QString linkedDirectory = tmp.filePath("Link to originals");
    fs::create_directory_symlink(native(files.path()), native(linkedDirectory), error);
#ifdef Q_OS_WIN
    // Windows can require Developer Mode or an elevated token for symlinks.
    const bool symlinkAllowed = !error;
#else
    CHECK(!error);
    const bool symlinkAllowed = true;
#endif
    if (symlinkAllowed) {
        CHECK(ManuscriptPath::same(sourceA, linkedDirectory + "/Original A.txt"));
        CHECK(ManuscriptPath::same(newA, linkedDirectory + "/New A.txt"));
    }

    Preferences prefs(tmp.filePath("path-identities.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.openDocumentInTab(sourceA));
    auto *first = window.editor();
    CHECK(window.openDocumentInTab(hardLink) && window.tabCount() == 1 && window.editor() == first);
    CHECK(window.openDocumentInTab(files.path() + "/./Original A.txt") && window.tabCount() == 1);
    if (symlinkAllowed) CHECK(window.openDocumentInTab(linkedDirectory + "/Original A.txt") && window.tabCount() == 1);
    CHECK(QFile::remove(sourceA));
    CHECK(!window.openDocumentInTab(missingB) && window.tabCount() == 1 && window.editor() == first);
    CHECK(window.openDocumentInTab(sourceB));
    auto *second = window.editor();
    CHECK(window.openDocumentInTab(sourceA) && window.editor() == first);
    first->insertLiteral(" pending A.");
    CHECK(until([&] { return first->hasFocus(); }));
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *chooserA = first->findChild<QFileDialog *>();
    CHECK(chooserA);
    CHECK(window.openDocumentInTab(sourceB));
    second->insertLiteral(" pending B.");
    CHECK(until([&] { return second->hasFocus(); }));
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *chooserB = second->findChild<QFileDialog *>();
    CHECK(chooserB);

    // Three choices without pumping the GUI event queue. A's Save As is
    // reserved until its worker reports the new path to the application.
    chooserA->fileSelected(newA);
    chooserB->fileSelected(newA);
    CHECK(window.findChild<QLabel *>("notice")->text().contains("already open"));
    chooserB->fileSelected(newB);
    chooserA->reject();
    chooserB->reject();
    CHECK(until([&] { return published(first) && published(second); }));
    CHECK(read(newA).contains("pending A.") && read(newB).contains("pending B."));
    CHECK(!QFile::exists(sourceA));
    CHECK(window.openDocumentInTab(newA) && window.editor() == first && window.tabCount() == 2);
    CHECK(window.close());
}

void testTabsSurviveFailedSave(const QTemporaryDir &tmp)
{
    const QString firstPath = tmp.filePath("First pending.txt");
    const QString changedPath = tmp.filePath("Second pending.txt");
    const QString rescuePath = tmp.filePath("Saved version.txt");
    const QByteArray config("literal-guillemets=true\nprivate-key=preserved\n");
    write(firstPath, "First pending text.");
    write(changedPath, "Second pending text.");
    write(changedPath + ".config", config);
    Preferences prefs(tmp.filePath("failed-close.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.openDocumentInTab(firstPath));
    window.editor()->insertLiteral(" Local first.");
    CHECK(window.openDocumentInTab(changedPath));
    auto *second = window.editor();
    second->insertLiteral(" Local second.");
    write(changedPath, "An external revision.");
    const auto earlierDrafts = DocumentStore::recoveryDrafts();
    CHECK(!window.close());
    pressChoice(window, "Save and Close");
    QString draft;
    CHECK(until([&] {
        for (const auto &candidate : DocumentStore::recoveryDrafts()) {
            if (earlierDrafts.contains(candidate) || !QFile::exists(candidate + ".config")) continue;
            auto contents = readIfAvailable(candidate);
            if (contents && contents->contains("Local second.")) { draft = candidate; return true; }
        }
        return false;
    }));
    CHECK(read(draft + ".config") == config && read(changedPath + ".config") == config);
    CHECK(window.isVisible() && window.tabCount() == 2 && window.editor() == second);
    CHECK(read(firstPath).contains("Local first.") && read(changedPath) == "An external revision.");
    CHECK(second->manuscriptText().contains("Local second."));
    window.activateManuscript(false);
    CHECK(until([&] { return second->hasFocus(); }));
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *chooser = second->findChild<QFileDialog *>();
    CHECK(chooser);
    chooser->hide();
    chooser->selectFile(rescuePath);
    chooser->show();
    static_cast<QDialog *>(chooser)->accept();
    CHECK(until([&] { return window.path() == rescuePath && !QFile::exists(draft + ".config"); }));
    CHECK(!QFile::exists(draft) && read(rescuePath + ".config") == config && read(rescuePath).contains("Local second."));
    CHECK(window.close());
}

void testRecoveredTabSettings(const QTemporaryDir &tmp)
{
    const QByteArray settings = "disable-general-title=true\nliteral-guillemets=true\nextra-key=retained\n";
    const QString draft = tmp.filePath("Recovered tab.txt");
    const QString promoted = tmp.filePath("Recovered novel.txt");
    write(draft, "Novel\nThe text.\n");
    write(draft + ".config", settings);
    Preferences prefs(tmp.filePath("recovery-tabs.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.openDocumentInTab(draft, true));
    auto *editor = window.editor();
    CHECK(window.path().isEmpty() && editor->literalGuillemets());
    CHECK(window.openDocumentInTab(draft) && window.tabCount() == 1 && window.editor() == editor);
    CHECK(editor->model().paragraphRole(0) == ParagraphRole::Body);
    CHECK(until([&] { return editor->hasFocus(); }));
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *chooser = editor->findChild<QFileDialog *>();
    CHECK(chooser);
    chooser->hide();
    chooser->selectFile(promoted);
    chooser->show();
    CHECK(chooser->selectedFiles() == QStringList{promoted});
    static_cast<QDialog *>(chooser)->accept();
    CHECK(until([&] { return window.path() == promoted && !QFileInfo::exists(draft + ".config"); }));
    CHECK(read(promoted + ".config") == settings);
    const QString discarded = tmp.filePath("Discarded recovery.txt");
    write(discarded, "Another draft.\n");
    write(discarded + ".config", settings);
    CHECK(window.openDocumentInTab(discarded, true));
    CHECK(window.editor()->literalGuillemets());
    window.closeCurrentTab();
    pressChoice(window, "Discard Unsaved Changes");
    CHECK(until([&] { return window.tabCount() == 1; }));
    CHECK(!QFileInfo::exists(discarded) && !QFileInfo::exists(discarded + ".config"));
    CHECK(window.close());
}

void testManuscriptTabs(const QTemporaryDir &tmp)
{
    const QString crumbs = tmp.filePath("Tab Crumbs.txt"), novel = tmp.filePath("Tab Novel.txt");
    const QString renamed = tmp.filePath("Tab Renamed.txt");
    write(crumbs, "Project//Note 1: First: red goose.\nNote 2: Last: blue goose.\n");
    QByteArray longNovel = "The Novel\nA red bicycle.\n";
    for (int row = 0; row < 100; ++row) longNovel += "A paragraph near the river.\n";
    write(novel, longNovel + "Final line.\n");
    write(crumbs + ".characters", "Ferdinand, Fred\n");
    Preferences prefs(tmp.filePath("tabs.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.openDocumentInTab(crumbs));
    CHECK(window.tabCount() == 1);
    auto *first = window.editor();
    CHECK(until([&] { return first->hasFocus(); }));
    key(Qt::Key_F, Qt::ControlModifier);
    auto *query = QApplication::focusWidget();
    CHECK(query && query->objectName() == "cw-find");
    static_cast<QLineEdit *>(query)->setText("goose");
    CHECK(until([&] { return first->textCursor().selectedText() == "goose"; }));
    const auto start = first->textCursor();
    CHECK(window.openDocumentInTab(novel));
    auto *second = window.editor();
    CHECK(window.tabCount() == 2 && first != second && second->model().isGeneric());
    CHECK(!query->isVisible());
    const auto passage = second->document()->findBlockByNumber(28);
    second->selectRange(passage.position(), passage.position() + 3, false);
    second->verticalScrollBar()->setValue(23);
    const int novelScroll = second->verticalScrollBar()->value();
    CHECK(novelScroll > 0);
    key(Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(window.editor() == first && !first->model().isGeneric() && query->isVisible());
    CHECK(first->textCursor().anchor() == start.anchor() && first->textCursor().position() == start.position());
    CHECK(!window.findChild<QLabel *>("documentMode")->text().contains("Generic"));
    key(Qt::Key_Tab, Qt::ControlModifier);
    CHECK(window.editor() == second);
    CHECK(second->verticalScrollBar()->value() == novelScroll && second->textCursor().selectedText() == "A p");
    second->openAtEnd();
    second->insertLiteral(" EDIT");
    CHECK(!window.openDocumentInTab(tmp.filePath("No such tab")));
    CHECK(window.editor() == second && window.tabCount() == 2 && second->manuscriptText().contains(" EDIT"));
    CHECK(window.openDocumentInTab(crumbs));
    CHECK(window.tabCount() == 2 && window.editor() == first);
    first->openAtEnd();
    first->insertLiteral(" END");
    first->selectRange(4, 8, false);

    // The file dialog belongs to its initiating manuscript throughout a tab switch.
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *chooser = first->findChild<QFileDialog *>();
    CHECK(chooser && chooser->isVisible());
    CHECK(window.openDocumentInTab(novel));
    chooser->fileSelected(crumbs); // Saving one's own path is allowed.
    chooser->reject();
    CHECK(until([&] {
        auto contents = published(first) ? readIfAvailable(crumbs) : std::nullopt;
        return contents && contents->contains(" END");
    }));
    CHECK(window.path() == novel && !read(novel).contains(" EDIT"));
    first->insertLiteral("x");

    // One confirmation for the window. Cancel preserves every tab and undo stack.
    CHECK(!window.close());
    auto *box = window.findChild<QMessageBox *>("confirmClose");
    CHECK(box && box->informativeText().contains(crumbs) && box->informativeText().contains(novel));
    pressChoice(window, "Keep Editing");
    CHECK(window.tabCount() == 2 && window.editor() == second);
    second->undo();
    CHECK(!second->manuscriptText().contains(" EDIT"));
    second->redo();
    CHECK(second->manuscriptText().contains(" EDIT"));
    CHECK(window.openDocumentInTab(crumbs));

    // A save targeting a different open manuscript is refused.
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    chooser = first->findChild<QFileDialog *>();
    CHECK(chooser);
    chooser->fileSelected(novel);
    chooser->reject();
    CHECK(window.path() == crumbs && !read(novel).contains(" END"));
    CHECK(window.findChild<QLabel *>("notice")->text().contains("already open"));
    first->selectRange(2, 7, false);
    key(Qt::Key_F4, Qt::ControlModifier);
    pressChoice(window, "Save and Close");
    CHECK(until([&] { return window.tabCount() == 1; }));
    key(Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(window.tabCount() == 2 && window.path() == crumbs);
    CHECK(window.editor()->textCursor().anchor() == 2 && window.editor()->textCursor().position() == 7);
    CHECK(window.openDocumentInTab(novel));

    QPointer<MainWindow> moved;
    window.makeWindow = [&] {
        moved = new MainWindow(prefs);
        return moved.data();
    };
    window.moveTabToNewWindow();
    CHECK(moved && moved->editor() == second && moved->tabCount() == 1 && window.tabCount() == 1);
    CHECK(until([&] { return second->hasFocus(); }));
    second->undo();
    CHECK(!second->manuscriptText().contains(" EDIT"));
    second->redo();
    CHECK(second->manuscriptText().contains(" EDIT"));
    CHECK(window.openDocumentInTab(novel));
    CHECK(window.path() == crumbs && moved->path() == novel && window.tabCount() == 1);
    CHECK(!moved->close());
    pressChoice(*moved, "Save and Close");
    CHECK(until([&] { return !moved || !moved->isVisible(); }));
    CHECK(read(novel).contains(" EDIT"));
    window.activateManuscript(false);
    CHECK(until([&] { return window.editor()->hasFocus(); }));
    key(Qt::Key_T, Qt::ControlModifier);
    auto *blank = window.editor();
    CHECK(window.tabCount() == 2 && window.path().isEmpty());
    blank->insertLiteral("An unsaved draft");
    CHECK(!window.close());
    pressChoice(window, "Save and Close");
    CHECK(until([&] { return blank->findChild<QFileDialog *>() != nullptr; }));
    chooser = blank->findChild<QFileDialog *>();
    chooser->reject();
    CHECK(window.tabCount() == 2 && window.isVisible());
    CHECK(!window.close());
    pressChoice(window, "Save and Close");
    CHECK(until([&] { return blank->findChild<QFileDialog *>() != nullptr; }));
    chooser = blank->findChild<QFileDialog *>();
    chooser->hide();
    chooser->selectFile(renamed);
    chooser->show();
    CHECK(chooser->selectedFiles() == QStringList{renamed});
    static_cast<QDialog *>(chooser)->accept();
    CHECK(until([&] { return !window.isVisible(); }));
    CHECK(read(renamed).contains("An unsaved draft"));
}

void testWindow(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Manuscript.txt");
    const QByteArray bytes = "Book//Note 1: Beginning: A reference to crumb 3 here.\n"
                             "Note 2: Onslaught #1: A middle.\n"
                             "Note 3: Finale: Last passage.\n";
    write(path, bytes);
    Preferences prefs(tmp.filePath("preferences.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(false);
    auto *editor = window.editor();
    CHECK(until([&] { return editor->hasFocus(); }));
    CHECK(!window.windowTitle().startsWith("•"));
    CHECK(editor->textCursor().blockNumber() == 2);
    CHECK(editor->cursorRect(QTextCursor(editor->document())).top() >= 0);
    CHECK(editor->viewport()->rect().contains(editor->cursorRect()));
    key(Qt::Key_1, Qt::ControlModifier);
    CHECK(editor->textCursor().selectedText() == "Note 3");
    CHECK(QApplication::clipboard()->text() == "note 3");
    key(Qt::Key_Escape);
    const int origin = bytes.indexOf("crumb 3") + 4;
    editor->jumpTo(origin);
    key(Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(editor->textCursor().blockNumber() == 2);
    key(Qt::Key_F2);
    CHECK(editor->textCursor().position() == origin);
    key(Qt::Key_F2, Qt::ShiftModifier);
    CHECK(editor->textCursor().blockNumber() == 2);

    key(Qt::Key_H, Qt::ControlModifier);
    auto *find = window.findChild<QLineEdit *>("cw-find");
    auto *replace = window.findChild<QLineEdit *>("cw-replacement");
    CHECK(find && replace && find->hasFocus());
    find->setText("middle");
    CHECK(until([&] { return editor->textCursor().selectedText() == "middle"; }));
    const auto before = editor->manuscriptText();
    key(Qt::Key_Y, Qt::ControlModifier);
    CHECK(editor->manuscriptText() == before && find->hasFocus());
    key(Qt::Key_H, Qt::ControlModifier);
    CHECK(replace->hasFocus());
    key(Qt::Key_H, Qt::ControlModifier);
    CHECK(find->hasFocus());
    key(Qt::Key_Escape);
    CHECK(editor->hasFocus() && !editor->textCursor().hasSelection());

    editor->openAtEnd();
    key(Qt::Key_X, {}, "x");
    CHECK(window.windowTitle().startsWith("•"));
    CHECK(!window.close());
    pressChoice(window, "Keep Editing");
    CHECK(window.isVisible() && read(path) == bytes);
    CHECK(!window.close());
    pressChoice(window, "Save and Close");
    CHECK(until([&] { return !window.isVisible(); }));
    CHECK(read(path).endsWith("Last passage.x\n"));
    CHECK(prefs.lastDocument() == path);
}

void testDuplicateHistory(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Duplicates.txt");
    const QByteArray bytes = "Colors//Crumb 7: Alpha: red.\nCrumb 7: Beta: blue.";
    write(path, bytes);
    Preferences prefs(tmp.filePath("duplicates.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(false);
    auto *ed = window.editor();
    CHECK(until([&] { return ed->hasFocus(); }));
    const int exact = bytes.indexOf("blue") + 2;
    ed->jumpTo(exact);
    ed->jumpTo(bytes.indexOf("red"));
    key(Qt::Key_3, Qt::ControlModifier | Qt::ShiftModifier);
    auto *tree = window.findChild<QTreeWidget *>("visitHistory");
    auto *historySearch = window.findChild<QLineEdit *>("historySearch");
    CHECK(tree && historySearch && historySearch->hasFocus());
    CHECK(tree->columnCount() == 1);
    const auto rows = tree->findItems("Beta", Qt::MatchExactly, 0);
    CHECK(rows.size() == 1);
    tree->setCurrentItem(rows.first());
    key(Qt::Key_Return);
    CHECK(ed->hasFocus() && ed->textCursor().position() == exact);
    key(Qt::Key_F2);
    CHECK(ed->textCursor().position() == bytes.indexOf("red"));
    key(Qt::Key_NumberSign, Qt::ControlModifier | Qt::ShiftModifier);
    key(Qt::Key_B, {}, "b");
    key(Qt::Key_E, {}, "e");
    key(Qt::Key_Return);
    CHECK(ed->textCursor().position() == exact);
    key(Qt::Key_periodcentered, Qt::ControlModifier | Qt::ShiftModifier);
    key(Qt::Key_A, {}, "a");
    key(Qt::Key_L, {}, "l");
    key(Qt::Key_Return);
    CHECK(ed->textCursor().position() == bytes.indexOf("red"));
    key(Qt::Key_F4);
    CHECK(ed->textCursor().selectedText() == "7");
    CHECK(window.findChild<QLabel *>("notice")->text().contains("Expected 2"));
    key(Qt::Key_F4, Qt::ShiftModifier);
    CHECK(ed->textCursor().blockNumber() == 0);
    key(Qt::Key_Escape);
    key(Qt::Key_Y, Qt::ControlModifier);
    CHECK(!ed->manuscriptText().contains("Alpha"));
    CHECK(!window.close());
    pressChoice(window, "Discard Unsaved Changes");
    CHECK(until([&] { return !window.isVisible(); }));
    CHECK(read(path) == bytes);
}

void testMentionCommands(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Goose.txt");
    const QByteArray text = "Book//Cranolo 1: Moonglass #1: Inspector Goose writes.\n"
                            "Cranolo 2: Next: The lunar auditor reads Cranolo 1 beside Moonglass.\n"
                            "Cranolo 3: Last: Goose is at the desk.";
    write(path, text);
    write(path + ".characters", "Inspector Goose, The lunar auditor, Goose\n");
    Preferences prefs(tmp.filePath("mention-shortcuts.ini"));
    prefs.settings().setValue("editor/backup", false);
    prefs.settings().setValue("editor/live-save", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(true);
    auto *editor = window.editor();
    CHECK(until([&] { return editor->hasFocus(); }));
    const int origin = editor->textCursor().position();
    const int first = text.indexOf("Inspector Goose");
    const int second = text.indexOf("The lunar auditor");
    const int third = text.lastIndexOf("Goose");
    auto click = [&](int index, Qt::KeyboardModifiers mods) {
        QTextCursor source(editor->document());
        source.setPosition(index);
        const auto point = editor->cursorRect(source).center() + QPoint(1, 0);
        CHECK(editor->viewport()->rect().contains(point));
        const auto global = editor->viewport()->mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, mods);
        QApplication::sendEvent(editor->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, mods);
        QApplication::sendEvent(editor->viewport(), &release);
    };
    click(second + 3, Qt::ControlModifier);
    CHECK(editor->textCursor().position() == first);
    key(Qt::Key_Left, Qt::AltModifier);
    CHECK(editor->textCursor().position() == origin);
    key(Qt::Key_Right, Qt::AltModifier);
    CHECK(editor->textCursor().position() == first);
    key(Qt::Key_F6); // No earlier mention adds no jump.
    CHECK(editor->textCursor().position() == first);
    key(Qt::Key_F6, Qt::ShiftModifier);
    CHECK(editor->textCursor().position() == second);
    click(second + 3, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(editor->textCursor().position() == third);
    key(Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(editor->textCursor().position() == second);
    key(Qt::Key_F6);
    CHECK(editor->textCursor().position() == first);
    key(Qt::Key_F2);
    CHECK(editor->textCursor().position() == second);
    click(text.lastIndexOf("Cranolo 1") + 2, Qt::ControlModifier);
    CHECK(editor->textCursor().blockNumber() == 0);
    key(Qt::Key_F2);
    CHECK(editor->textCursor().position() == second);
    const int genreTitle = text.indexOf("Moonglass"), genreBody = text.lastIndexOf("Moonglass");
    editor->jumpTo(genreBody + 2, false);
    key(Qt::Key_F6);
    CHECK(editor->textCursor().position() == genreTitle);
    key(Qt::Key_F6, Qt::ShiftModifier);
    CHECK(editor->textCursor().position() == genreBody);
    key(Qt::Key_4, Qt::ControlModifier | Qt::ShiftModifier);
    auto *genreSearch = window.findChild<QLineEdit *>("genresSearch");
    CHECK(genreSearch && genreSearch->hasFocus());
    genreSearch->setText("moongl");
    key(Qt::Key_Return, Qt::ControlModifier);
    auto *results = window.findChild<QTableView *>("synonymMatches");
    CHECK(results && until([&] { return results->isVisible() && results->model()->rowCount() == 2; }));
    key(Qt::Key_Return);
    CHECK(editor->textCursor().selectedText() == "Moonglass");
    // Deleting the first-line separator switches modes in the same editor.
    auto mode = editor->textCursor();
    mode.setPosition(4);
    mode.setPosition(6, QTextCursor::KeepAnchor);
    mode.removeSelectedText();
    CHECK(editor->model().isGeneric());
    editor->jumpTo(genreBody, false);
    key(Qt::Key_F6);
    CHECK(editor->textCursor().position() == genreBody);
    key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(!editor->model().isGeneric());
    editor->jumpTo(genreBody + 2, false);
    key(Qt::Key_F6);
    CHECK(editor->textCursor().position() == genreTitle);
    editor->jumpTo(second, false);
    key(Qt::Key_S, Qt::ControlModifier);
    CHECK(until([&] { return !window.windowTitle().startsWith(QStringLiteral("• ")); }));
    key(Qt::Key_U, Qt::ControlModifier);
    auto *picker = window.findChild<QDialog *>("symbolPicker");
    CHECK(picker && until([&] { return picker->findChild<QLineEdit *>("symbolSearch")->hasFocus(); }));
    key(Qt::Key_Escape);
    CHECK(until([&] { return !picker->isVisible(); }));

    const auto generic = tmp.filePath("GooseNovel.txt");
    write(generic, "The Goose Novel\nInspector Goose writes.\nThe lunar auditor reads.");
    write(generic + ".characters", "Inspector Goose, The lunar auditor\n");
    CHECK(window.loadDocument(generic));
    window.activateManuscript(false);
    CHECK(until([&] { return editor->hasFocus(); }));
    editor->jumpTo(editor->manuscriptText().indexOf("lunar") + 1);
    key(Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(editor->textCursor().position() == editor->manuscriptText().indexOf("Inspector"));
    key(Qt::Key_F, Qt::ControlModifier);
    auto *find = window.findChild<QLineEdit *>("cw-find");
    find->setText("lunar auditor");
    CHECK(until([&] { return editor->textCursor().selectedText() == "lunar auditor"; }));
    const auto beforeAppend = editor->textCursor();
    const int oldAnchor = beforeAppend.anchor();
    const int oldPosition = beforeAppend.position();
    key(Qt::Key_N, Qt::ControlModifier);
    CHECK(editor->hasFocus() && !find->isVisibleTo(&window));
    key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(editor->textCursor().selectedText() == "lunar auditor");
    CHECK(editor->textCursor().anchor() == oldAnchor && editor->textCursor().position() == oldPosition);
    if (!window.close()) pressChoice(window, "Discard Unsaved Changes");
}

void testGenreSpelling(const QTemporaryDir &tmp)
{
    const QString path = tmp.filePath("GenreSpelling.txt");
    const auto manuscript = QStringLiteral("Book//Crumb 1: Qzäß Blepik #1: Here is QZÄSS BLEPIK. "
                                           "Elsewhere: Blepik.\nCrumb 2: Note: Blepik.");
    write(path, manuscript.toUtf8());
    Preferences prefs(tmp.filePath("genre-spelling.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/spell-check", true);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(true);
    auto *editor = window.editor();
    auto errors = [&] {
        QList<int> positions;
        for (const auto &sel : editor->extraSelections())
            if (sel.format.underlineStyle() == QTextCharFormat::WaveUnderline)
                positions.append(sel.cursor.selectionStart());
        return positions;
    };
    const int first = int(manuscript.indexOf("Elsewhere: Blepik")) + 11;
    CHECK(until([&] { return errors() == QList<int>{first, int(manuscript.lastIndexOf("Blepik"))}; }));
    editor->jumpTo(0);
    key(Qt::Key_F8);
    auto *review = window.findChild<QDialog *>("spellingReview");
    CHECK(review && until([&] { return editor->textCursor().selectionStart() == first; }));
    review->findChild<QLineEdit *>("spellingReplacement")->setText("reed");
    review->findChild<QPushButton *>("spellingReplaceAll")->click();
    CHECK(until([&] { return editor->manuscriptText().count("reed") == 2; }));
    CHECK(editor->manuscriptText().contains(QStringLiteral("Qzäß Blepik #1: Here is QZÄSS BLEPIK.")));
    review->close();
    editor->undo();
    CHECK(editor->manuscriptText() == manuscript);
    QTextCursor prefix(editor->document());
    prefix.setPosition(6, QTextCursor::KeepAnchor);
    prefix.removeSelectedText();
    CHECK(editor->model().isGeneric() && until([&] { return errors().size() > 2; }));
    editor->undo();
    CHECK(!editor->model().isGeneric() && until([&] { return errors().size() == 2; }));
}

void testWordCommandsAndStatus(const QTemporaryDir &tmp)
{
    Preferences prefs(tmp.filePath("word-shortcuts.ini"));
    CHECK(!prefs.enabled("copy-deleted"));
    prefs.settings().setValue("editor/copy-deleted", true);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(1050, 650);
    window.activateManuscript(false);
    auto *editor = window.editor();
    editor->loadText("The well-read doctor.");
    editor->jumpTo(7, false);
    CHECK(editor->copiesDeletedText() && until([&] { return editor->hasFocus(); }));
    auto *link = window.findChild<QToolButton *>("crumbsWebsite");
    CHECK(link && until([&] { return link->isVisible(); }));
    CHECK(qAbs(link->mapTo(&window, link->rect().center()).x() - window.rect().center().x()) <= 2);
    QTextBrowser browser;
    QList<QUrl> urls;
    QObject::connect(&browser, &QTextBrowser::sourceChanged, [&](const QUrl &url) { urls << url; });
    QDesktopServices::setUrlHandler("https", &browser, "sourceChanged");
    link->click();
    CHECK(urls.size() == 1 && urls.last() == QUrl("https://www.thecrumbs.org/"));
    editor->setFocus();
    key(Qt::Key_2, Qt::ControlModifier);
    CHECK(editor->textCursor().selectedText() == "well-read");
    for (const int suffix : {Qt::Key_D, Qt::Key_S, Qt::Key_T}) {
        key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(!link->isVisible());
        key(suffix);
    }
    CHECK(urls.size() == 4 && urls[1].path() == "/dictionary/well-read"
          && urls[2].fragment() == "synonyms" && urls[3].path() == "/thesaurus/well-read");
    key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    key(Qt::Key_Escape);
    CHECK(link->isVisible() && urls.size() == 4);
    window.resize(270, 350);
    CHECK(until([&] { return !link->isVisible(); }));
    QDesktopServices::unsetUrlHandler("https");
}

void testManuscriptLinks(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Links.txt");
    const QString text = QStringLiteral("Book//Crumb 1: T: Goose writes.\nCrumb 2: www.example.org/Goose: End.");
    write(path, text.toUtf8());
    write(path + ".characters", "Goose\n");
    Preferences prefs(tmp.filePath("link-shortcuts.ini"));
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(true);
    auto *editor = window.editor();
    CHECK(until([&] { return editor->hasFocus(); }));

    // Qt's URL handler can invoke an existing QUrl signal. Observe the exact
    // target, without launching a browser or issuing a network request.
    QTextBrowser receiver;
    QList<QUrl> opened;
    QObject::connect(&receiver, &QTextBrowser::sourceChanged, [&](const QUrl &url) { opened << url; });
    QDesktopServices::setUrlHandler("https", &receiver, "sourceChanged");
    const QUrl expected("https://www.example.org/Goose");
    const int name = text.lastIndexOf("Goose");
    editor->jumpTo(name + 2);
    auto formatAt = [&](int position) {
        const auto block = editor->document()->findBlock(position);
        for (const auto &span : block.layout()->formats())
            if (position >= block.position() + span.start && position < block.position() + span.start + span.length)
                return span.format;
        return QTextCharFormat();
    };
    CHECK(until([&] { return formatAt(name).fontUnderline(); }));
    CHECK(formatAt(name).fontWeight() == QFont::Bold && !formatAt(name + 5).fontUnderline());
    key(Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(opened.size() == 1 && opened.last() == expected && editor->textCursor().position() == name + 2);

    auto point = editor->cursorRect().center() + QPoint(1, 0);
    auto global = editor->viewport()->mapToGlobal(point);
#ifdef Q_OS_MACOS
    const auto modifier = Qt::MetaModifier;
#else
    const auto modifier = Qt::ControlModifier;
#endif
    QMouseEvent click(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, modifier);
    QApplication::sendEvent(editor->viewport(), &click);
    CHECK(opened.size() == 2 && opened.last() == expected && editor->textCursor().position() == name + 2);

    QTimer::singleShot(0, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        CHECK(menu && !menu->actions().isEmpty());
        auto *action = menu->actions().constFirst();
        CHECK(action->text() == "&Open Link");
        action->trigger();
        menu->close();
    });
    QContextMenuEvent request(QContextMenuEvent::Mouse, point, global);
    QApplication::sendEvent(editor->viewport(), &request);
    CHECK(opened.size() == 3 && opened.last() == expected);

    const auto generic = tmp.filePath("LinkNovel.txt");
    write(generic, "A Novel\nRead https://example.org/a_(b).");
    CHECK(window.loadDocument(generic));
    window.activateManuscript(false);
    CHECK(until([&] { return editor->hasFocus(); }));
    const int end = editor->manuscriptText().lastIndexOf(u'.');
    editor->jumpTo(end);
    CHECK(until([&] { return formatAt(end - 1).fontUnderline(); }));
    CHECK(!formatAt(end).fontUnderline());
    key(Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(opened.size() == 4 && opened.last() == QUrl("https://example.org/a_(b)"));
    QDesktopServices::unsetUrlHandler("https");
    CHECK(window.close());
}

void testWritingTools(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Writing.txt");
    write(path, QStringLiteral("A Book//Crumb 1: T: red—blue / fox.\nCrumb 2: U: end\n").toUtf8());
    write(path + ".characters", "\"Fox, Captain\", Red Fox\n");
    write(path + ".recurring", "red, scarlet\n");
    write(path + ".dictionary", "Zorbo\n");
    Preferences prefs(tmp.filePath("writing.ini"));
    prefs.settings().setValue("editor/live-save", false);
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(false);
    auto *ed = window.editor();
    CHECK(until([&] { return ed->hasFocus(); }));

    key(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    auto *stats = window.findChild<QDialog *>("statisticsDialog");
    CHECK(stats && stats->isVisible() && !stats->isModal());
    const auto figure = [&](const char *name) { return stats->findChild<QLabel *>(name)->text(); };
    CHECK(figure("statisticsCrumbs") == "2");
    CHECK(figure("statisticsWords") == "12");
    CHECK(figure("statisticsAverage") == QLocale().toString(6.0, 'f', 2));

    window.activateManuscript(false);
    CHECK(until([&] { return ed->hasFocus(); }));
    key(Qt::Key_P, Qt::ControlModifier);
    auto *find = window.findChild<QLineEdit *>("cw-find");
    CHECK(find && find->hasFocus());
    auto *words = window.findChild<QCheckBox *>("cw-search-word");
    auto *wrap = window.findChild<QCheckBox *>("cw-search-wrap");
    const bool oldWords = words->isChecked(), oldWrap = wrap->isChecked();
    key(Qt::Key_W, Qt::ControlModifier);
    key(Qt::Key_W, Qt::AltModifier);
    CHECK(words->isChecked() != oldWords && wrap->isChecked() != oldWrap && window.isVisible());
    ed->setFocus();
    ed->openAtEnd();
    key(Qt::Key_W, Qt::ControlModifier);
    CHECK(!ed->manuscriptText().contains("end"));
    CHECK(figure("statisticsWords") == "11");
    key(Qt::Key_R, Qt::ControlModifier);
    CHECK(find->hasFocus());
    key(Qt::Key_L, Qt::ControlModifier);
    CHECK(window.findChild<QLineEdit *>("cw-crumb-number")->hasFocus());
    key(Qt::Key_N, Qt::ControlModifier);
    CHECK(ed->hasFocus() && ed->textCursor().blockNumber() == 2);
    CHECK(figure("statisticsCrumbs") == "3");
    CHECK(window.findChild<QLabel *>("todayCount")->text().contains("1"));
    key(Qt::Key_Escape);

    key(Qt::Key_Exclam, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(!ed->hasFocus());
    key(Qt::Key_Return, Qt::ControlModifier);
    auto *names = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    CHECK(names && names->activeAction()->text() == "Fox, Captain");
    QKeyEvent chooseName(QEvent::KeyPress, Qt::Key_Return, {});
    QApplication::sendEvent(names, &chooseName);
    CHECK(until([&] { return ed->hasFocus(); }));
    CHECK(ed->textCursor().block().text().endsWith("Fox, Captain"));

    const int nameStart = ed->manuscriptText().indexOf("Fox, Captain");
    ed->selectRange(nameStart, nameStart + 12, false);
    for (const auto section : {Qt::Key_1, Qt::Key_2}) {
        key(section, Qt::ControlModifier | Qt::ShiftModifier);
#ifdef Q_OS_MACOS
        key(Qt::Key_I, Qt::ControlModifier);
#else
        key(Qt::Key_Insert);
#endif
        auto *form = window.findChild<QDialog *>(section == Qt::Key_1 ? "charactersEditDialog" : "recurringEditDialog");
        CHECK(form && form->isVisible());
        auto *field = form->findChild<QLineEdit *>("synonymText");
        CHECK(until([&] { return field->hasFocus(); }));
        CHECK(field->selectedText() == "\"Fox, Captain\"");
        if (section == Qt::Key_1) {
            key(Qt::Key_Escape);
        } else {
            key(Qt::Key_Return);
            CHECK(read(path + ".recurring").contains("\"Fox, Captain\""));
        }
        CHECK(ed->textCursor().selectedText() == "Fox, Captain");
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.activateManuscript(false);
        CHECK(until([&] { return ed->hasFocus(); }));
    }

    const auto copy = tmp.filePath("Copy.txt");
    key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    auto *chooser = window.findChild<QFileDialog *>();
    CHECK(chooser && chooser->isVisible());
    // Qt preserves whatever the focused filename field currently contains
    // when selectFile() is called on a visible widget dialog.
    chooser->hide();
    chooser->selectFile(copy);
    chooser->show();
    CHECK(chooser->selectedFiles() == QStringList{copy});
    static_cast<QDialog *>(chooser)->accept();
    CHECK(until([&] { return window.path() == copy && QFile::exists(copy + ".dictionary"); }));
    for (auto suffix : {".characters", ".recurring", ".dictionary"})
        CHECK(read(path + suffix) == read(copy + suffix));
    CHECK(read(path).endsWith("U: end\n"));
    stats->close();
    window.activateManuscript(false);
    CHECK(until([&] { return ed->hasFocus(); }));
    key(Qt::Key_W, Qt::AltModifier);
    CHECK(until([&] { return !window.isVisible(); }));
}

void testGroupedLiveEdits(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Live.txt");
    const QByteArray original = "The fox";
    write(path, original);
    Preferences prefs(tmp.filePath("live.ini"));
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(false);
    auto *ed = window.editor();
    CHECK(until([&] { return ed->hasFocus(); }));
    key(Qt::Key_A, {}, "a");
    CHECK(until([&] { return published(ed) && readIfAvailable(path) == original + "a\n"; }));
    key(Qt::Key_B, {}, "b");
    key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(ed->manuscriptText() == original && !ed->document()->isUndoAvailable());
    key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(ed->manuscriptText() == original + "ab");
    auto *status = window.findChild<QLabel *>("documentStatus");
    key(Qt::Key_Space, Qt::ControlModifier);
    key(Qt::Key_Q, Qt::ShiftModifier, "Q");
    key(Qt::Key_F, {}, "f");
    CHECK(until([&] { return published(ed) && readIfAvailable(path) == original + "ab\n"; }));
    CHECK(status->text().contains("Qf") && ed->viSelectionActive());
    key(Qt::Key_O, {}, "o");
    key(Qt::Key_X, {}, "x");
    key(Qt::Key_Return);
    CHECK(ed->textCursor().selectedText() == "foxab" && !ed->viSelectionActive());
    CHECK(window.close());
}

void testKeyboardLayouts(const QTemporaryDir &tmp)
{
    Preferences prefs(tmp.filePath("key-layouts.ini"));
    prefs.settings().setValue("editor/live-save", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.activateManuscript(false);
    auto *editor = window.editor();
    CHECK(until([&] { return editor->hasFocus(); }));
    const auto panel = [&](int keycode, const char *field) {
        key(keycode, Qt::ControlModifier | Qt::ShiftModifier);
        auto *search = window.findChild<QLineEdit *>(field);
        CHECK(search && search->hasFocus());
        editor->setFocus();
    };
    // Qt reports either a digit or the character printed on that shifted
    // key. English and Spanish disagree on the second and third keys.
    panel(Qt::Key_At, "recurringSearch");
    panel(Qt::Key_QuoteDbl, "recurringSearch");
    panel(Qt::Key_NumberSign, "historySearch");
    panel(Qt::Key_periodcentered, "historySearch");
    key(Qt::Key_At, Qt::GroupSwitchModifier, "@");
#ifdef Q_OS_MACOS
    key(Qt::Key_3, Qt::AltModifier, "#");
    key(Qt::Key_W, Qt::AltModifier, QStringLiteral("∑"));
    CHECK(editor->manuscriptText() == QStringLiteral("@#∑") && window.isVisible());
    key(Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(editor->viSelectionActive());
    key(Qt::Key_Escape);
    key(Qt::Key_A, Qt::MetaModifier);
    CHECK(editor->textCursor().hasSelection());
    key(Qt::Key_C, Qt::MetaModifier);
    CHECK(QApplication::clipboard()->text() == editor->manuscriptText());
#else
    key(Qt::Key_3, Qt::ControlModifier | Qt::AltModifier, "#");
    CHECK(editor->manuscriptText() == "@#" && editor->hasFocus());
#endif
}

void testGenericWindow(const QTemporaryDir &tmp)
{
    const QString path = tmp.filePath("Moon.txt");
    const QByteArray bytes = "Cartography\n\nThe Arrival\n-----------\nA goose counted twice.\nThe moon waited.\n";
    write(path, bytes);
    write(path + ".bookmarks",
          R"({"version":1,"bookmarks":[{"category":"Scenes","name":"Arrival","line":3}]})");
    Preferences prefs(tmp.filePath("generic.ini"));
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(false);
    auto *ed = window.editor();
    CHECK(until([&] { return ed->hasFocus(); }));
    auto *mode = window.findChild<QLabel *>("documentMode");
    CHECK(mode && mode->text() == "Generic mode");
    key(Qt::Key_L, Qt::ControlModifier);
    auto *number = window.findChild<QLineEdit *>("cw-crumb-number");
    CHECK(number && number->accessibleName() == "Line number" && number->hasFocus());
    number->setText("2");
    key(Qt::Key_Return);
    CHECK(ed->hasFocus() && ed->textCursor().blockNumber() == 1);
    key(Qt::Key_Dollar, Qt::ControlModifier | Qt::ShiftModifier);
    auto *titles = window.findChild<QLineEdit *>("genresSearch");
    CHECK(titles && titles->hasFocus());
    titles->setText("rri");
    key(Qt::Key_Return, Qt::ControlModifier);
    CHECK(titles->text().isEmpty() && ed->hasFocus() && ed->textCursor().blockNumber() == 2);
    key(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    auto *stats = window.findChild<QDialog *>("statisticsDialog");
    auto *count = stats ? stats->findChild<QLabel *>("statisticsCrumbs") : nullptr;
    CHECK(count && count->text() == "4" && count->accessibleName() == "Paragraphs");
    window.activateManuscript(false);
    CHECK(until([&] { return ed->hasFocus(); }));
    ed->jumpTo(0);
    ed->insertLiteral("Cranolos//");
    CHECK(mode->text() == "Crumb mode" && count->accessibleName() == "Crumbs");
    ed->undo();
    CHECK(mode->text() == "Generic mode" && count->text() == "4");
    ed->jumpTo(0);
    ed->insertLiteral("Preface.\n");
    auto savedLine = [&] {
        auto bytes = readIfAvailable(path + ".bookmarks");
        if (!bytes) return 0;
        auto json = QJsonDocument::fromJson(*bytes).object();
        auto rows = json.value("bookmarks").toArray();
        return rows.isEmpty() ? -1 : rows.first().toObject().value("line").toInt();
    };
    CHECK(until([&] { return published(window.editor()) && readIfAvailable(path) == "Preface.\n" + bytes && savedLine() == 4; }));
    ed->undo();
    CHECK(until([&] { return published(window.editor()) && readIfAvailable(path) == bytes && savedLine() == 3; }));
    stats->close();
    CHECK(window.close());
}

void testDialogs(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Stats.txt");
    QByteArray manuscript = "Birds//";
    for (int i = 1; i <= 1234; ++i)
        manuscript += "Crumb " + QByteArray::number(i) + ": : owl\n";
    write(path, manuscript);
    Preferences prefs(tmp.filePath("dialogs.ini"));
    prefs.settings().setValue("editor/backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.loadDocument(path));
    window.activateManuscript(false);
    CHECK(until([&] { return window.editor()->hasFocus(); }));
    const QLocale desktop;
    QLocale::setDefault(QLocale::c());
    key(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    auto *stats = window.findChild<QDialog *>("statisticsDialog");
    CHECK(stats && until([&] { return stats->isActiveWindow(); }));
    auto *crumbs = stats->findChild<QLabel *>("statisticsCrumbs");
    auto *words = stats->findChild<QLabel *>("statisticsWords");
    auto *average = stats->findChild<QLabel *>("statisticsAverage");
    CHECK(crumbs->text() == "1,234" && words->text() == "4,937" && average->text() == "4.00");
    CHECK(crumbs->alignment().testFlag(Qt::AlignRight));
    CHECK(crumbs->geometry().right() == words->geometry().right() &&
          words->geometry().right() == average->geometry().right());
    CHECK(stats->layout()->contentsMargins().left() >= 20);
    key(Qt::Key_C, Qt::ControlModifier);
    CHECK(QApplication::clipboard()->text().contains("Total words: 4,937"));
    key(Qt::Key_Escape);
    QLocale::setDefault(QLocale("es_ES"));
    {
        StatisticsDialog spanish(window.editor()->model(), &window);
        CHECK(spanish.findChild<QLabel *>("statisticsCrumbs")->text() == "1.234");
        CHECK(spanish.findChild<QLabel *>("statisticsAverage")->text() == "4,00");
    }
    QLocale::setDefault(desktop);
    window.activateManuscript(false);
    CHECK(until([&] { return window.editor()->hasFocus(); }));
    key(Qt::Key_F10);
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    CHECK(menu);
    menu->hide();
    for (auto *action : window.findChildren<QAction *>())
        if (action->menuRole() == QAction::AboutRole) action->trigger();
    auto *about = window.findChild<QDialog *>("aboutDialog");
    CHECK(about && until([&] { return about->isActiveWindow(); }) && !about->isModal());
    QStringList labels;
    for (auto *label : about->findChildren<QLabel *>()) labels.append(label->text());
    CHECK(labels.contains(QString("Crumb Writer %1").arg(CrumbWriter::version)));
    CHECK(labels.join(u' ').contains("Created and directed by <b>Alfredo Llaquet-Alsina</b>. Implemented by <b>ChatGPT</b>."));
    CHECK(labels.join(u' ').contains(qVersion()));
    auto *link = about->findChild<QLabel *>("repositoryLink");
    CHECK(link && link->hasFocus() && link->text().contains(CrumbWriter::repository));
    // Capture activation without opening the runner's browser.
    QObject::disconnect(link, &QLabel::linkActivated, about, nullptr);
    QString opened;
    QObject::connect(link, &QLabel::linkActivated, about, [&](const QString &url) { opened = url; });
    key(Qt::Key_Return);
    CHECK(opened == CrumbWriter::repository);
    key(Qt::Key_L, Qt::AltModifier);
    CHECK(until([&] { return about->findChild<QDialog *>("licenseDialog"); }));
    auto *license = about->findChild<QDialog *>("licenseDialog");
    CHECK(until([&] { return license->isActiveWindow(); }) && !license->isModal());
    auto *terms = license->findChild<QPlainTextEdit *>();
    QFile original(":/license/LICENSE");
    CHECK(original.open(QIODevice::ReadOnly | QIODevice::Text));
    CHECK(terms && terms->isReadOnly() && terms->toPlainText() == QString::fromUtf8(original.readAll()));
    CHECK(terms->toPlainText().contains("WITHOUT WARRANTY"));
    key(Qt::Key_Escape);
    CHECK(until([&] { return about->isActiveWindow(); }));
    key(Qt::Key_Escape);
    CHECK(window.isVisible() && window.close());
}

void testMenuAccess(const QTemporaryDir &tmp)
{
    Preferences prefs(tmp.filePath("menus.ini"));
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(950, 600);
    window.activateManuscript(false);
    auto *toolbar = window.findChild<QToolBar *>("manuscriptToolbar");
    CHECK(toolbar);
    auto *bar = window.findChild<QMenuBar *>("manuscriptMenuBar");
    CHECK(bar && bar->actions().size() == 8);
    auto *fileMenu = bar->actions().front()->menu();
    CHECK(fileMenu && fileMenu->title().contains("File"));
    const auto path = tmp.filePath("menu-manuscript.txt");
    write(path, "Book//Crumb 1: Bird: Wings.");
    CHECK(window.loadDocument(path));
    auto dismissMenu = [](QMenu *menu) {
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(menu, &escape);
        CHECK(until([&] { return !menu->isVisible(); }));
    };
    for (const char *name : {"charactersMenu", "recurringMenu"}) {
        auto *button = window.findChild<QToolButton *>(name);
        CHECK(button && button->menu() && !button->accessibleName().isEmpty());
        // The offscreen plugin can deliver focus-restoration events after a
        // popup has hidden. Establish the editing window before tabbing again.
        window.activateManuscript(false);
        CHECK(until([&] { return window.editor()->hasFocus() && button->isVisible(); }));
        {
            const bool recurring = button->objectName() == "recurringMenu";
            auto *field = window.findChild<QLineEdit *>(recurring ? "recurringSearch" : "charactersSearch");
            key(recurring ? Qt::Key_2 : Qt::Key_1, Qt::ControlModifier | Qt::ShiftModifier);
            CHECK(field && until([&] { return field->hasFocus(); }));
            key(Qt::Key_Backtab, Qt::ShiftModifier);
        }
        if (!until([&] { return button->hasFocus(); })) {
            std::fprintf(stderr, "%s did not receive keyboard focus (policy %d, current widget %s).\n",
                         name, int(button->focusPolicy()), QApplication::focusWidget() ?
                         qPrintable(QApplication::focusWidget()->objectName()) : "none");
            CHECK(false);
        }
        key(Qt::Key_Space);
        CHECK(until([&] { return button->menu()->isVisible() && button->menu()->activeAction(); }));
        dismissMenu(button->menu());
    }

    window.resize(190, 420);
    window.activateManuscript(false);
    CHECK(until([&] { return window.editor()->hasFocus(); }));
    key(Qt::Key_F10);
    CHECK(until([&] { return fileMenu->isVisible(); }));
    dismissMenu(fileMenu);
    CHECK(window.close());
}
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("XDG_DATA_HOME", QFile::encodeName(tmp.filePath("application-data")));
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    testWindow(tmp);
    testManuscriptTabs(tmp);
    testManuscriptPaths(tmp);
    testRecoveredTabSettings(tmp);
    testTabsSurviveFailedSave(tmp);
    testDuplicateHistory(tmp);
    testMentionCommands(tmp);
    testGenreSpelling(tmp);
    testWordCommandsAndStatus(tmp);
    testManuscriptLinks(tmp);
    testWritingTools(tmp);
    testGroupedLiveEdits(tmp);
    testKeyboardLayouts(tmp);
    testGenericWindow(tmp);
    testDialogs(tmp);
    testMenuAccess(tmp);
    std::puts("Main window keyboard, reference jump, duplicate history and close choices passed.");
}
