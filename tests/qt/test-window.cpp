#include "mainwindow.h"
#include "buildinfo.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "statisticsdialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
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
    CHECK(tree->columnCount() == 1);
    const auto rows = tree->findItems("Beta", Qt::MatchExactly, 0);
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

    key(Qt::Key_1, Qt::AltModifier);
    CHECK(!ed->hasFocus());
    key(Qt::Key_Return, Qt::ControlModifier);
    CHECK(until([&] { return ed->hasFocus(); }));
    CHECK(ed->textCursor().block().text().endsWith("Fox, Captain"));

    const int nameStart = ed->manuscriptText().indexOf("Fox, Captain");
    ed->selectRange(nameStart, nameStart + 12, false);
    for (const auto section : {Qt::Key_1, Qt::Key_2}) {
        key(section, Qt::AltModifier);
        key(Qt::Key_Insert);
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
    chooser->selectFile(copy);
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
    const QByteArray original = "Crumb 1: : fox";
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
    CHECK(until([&] { return read(path) == original + 'a'; }));
    key(Qt::Key_B, {}, "b");
    key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(ed->manuscriptText() == original && !ed->document()->isUndoAvailable());
    key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(ed->manuscriptText() == original + "ab");
    auto *status = window.findChild<QLabel *>("documentStatus");
    key(Qt::Key_Space, Qt::ControlModifier);
    key(Qt::Key_Q, Qt::ShiftModifier, "Q");
    key(Qt::Key_F, {}, "f");
    CHECK(until([&] { return read(path) == original + "ab"; }));
    CHECK(status->text().contains("Qf") && ed->viSelectionActive());
    key(Qt::Key_O, {}, "o");
    key(Qt::Key_X, {}, "x");
    key(Qt::Key_Return);
    CHECK(ed->textCursor().selectedText() == "foxab" && !ed->viSelectionActive());
    CHECK(window.close());
}

void testDialogs(const QTemporaryDir &tmp)
{
    const auto path = tmp.filePath("Stats.txt");
    QByteArray manuscript;
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
    CHECK(crumbs->text() == "1,234" && words->text() == "4,936" && average->text() == "4.00");
    CHECK(crumbs->alignment().testFlag(Qt::AlignRight));
    CHECK(crumbs->geometry().right() == words->geometry().right() &&
          words->geometry().right() == average->geometry().right());
    CHECK(stats->layout()->contentsMargins().left() >= 20);
    key(Qt::Key_C, Qt::ControlModifier);
    CHECK(QApplication::clipboard()->text().contains("Total words: 4,936"));
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
    QKeyEvent mnemonic(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    QApplication::sendEvent(menu, &mnemonic);
    auto *about = window.findChild<QDialog *>("aboutDialog");
    CHECK(about && until([&] { return about->isActiveWindow(); }) && !about->isModal());
    QStringList labels;
    for (auto *label : about->findChildren<QLabel *>()) labels.append(label->text());
    CHECK(labels.contains(QString("Crumb Writer %1").arg(CrumbWriter::version)));
    CHECK(labels.join(u' ').contains("Programmed by ChatGPT for Alfredo Llaquet-Alsina"));
    CHECK(labels.join(u' ').contains(qVersion()));
    auto *link = about->findChild<QLabel *>("repositoryLink");
    CHECK(link && link->hasFocus() && link->text().contains(CrumbWriter::repository));
    // Capture activation without opening the runner's browser.
    QObject::disconnect(link, &QLabel::linkActivated, about, nullptr);
    QString opened;
    QObject::connect(link, &QLabel::linkActivated, about, [&](const QString &url) { opened = url; });
    key(Qt::Key_Return);
    CHECK(opened == CrumbWriter::repository);
    key(Qt::Key_Escape);
    CHECK(window.isVisible() && window.close());
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
    testWritingTools(tmp);
    testGroupedLiveEdits(tmp);
    testDialogs(tmp);
    std::puts("Main window keyboard, reference jump, duplicate history and close choices passed.");
}
