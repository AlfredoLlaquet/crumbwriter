#include "mainwindow.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QThread>
#include <QTreeWidget>
#include <cstdio>
#include <cstdlib>

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

void key(int code, Qt::KeyboardModifiers mods = {}, const QString &text = {})
{
    auto *target = QApplication::focusWidget();
    CHECK(target);
    QKeyEvent press(QEvent::KeyPress, code, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, code, mods, text);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile out(path);
    CHECK(out.open(QIODevice::WriteOnly));
    CHECK(out.write(bytes) == bytes.size());
}

QByteArray read(const QString &path)
{
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    return f.readAll();
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
    key(Qt::Key_1, Qt::ControlModifier);
    CHECK(editor->textCursor().selectedText() == "Note 3");
    CHECK(QApplication::clipboard()->text() == "Note 3");
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
    const QByteArray bytes = "Crumb 7: Alpha: red.\nCrumb 7: Beta: blue.";
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
    key(Qt::Key_F6);
    auto *tree = window.findChild<QTreeWidget *>("visitHistory");
    CHECK(tree && tree->hasFocus());
    const auto rows = tree->findItems("Beta", Qt::MatchExactly, 1);
    CHECK(rows.size() == 1);
    tree->setCurrentItem(rows.first());
    key(Qt::Key_Return);
    CHECK(ed->hasFocus() && ed->textCursor().position() == exact);
    key(Qt::Key_F2);
    CHECK(ed->textCursor().position() == bytes.indexOf("red"));
    key(Qt::Key_F6);
    key(Qt::Key_B, {}, "b");
    key(Qt::Key_E, {}, "e");
    key(Qt::Key_Return);
    CHECK(ed->textCursor().position() == exact);
    key(Qt::Key_F6);
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
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    testWindow(tmp);
    testDuplicateHistory(tmp);
    std::puts("Main window keyboard, reference jump, duplicate history and close choices passed.");
}
