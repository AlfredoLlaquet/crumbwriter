#include "bookmarkfile.h"
#include "bookmarks.h"

#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QKeyEvent>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeWidget>

namespace {

void check(bool condition, const char *reason)
{
    if (!condition)
        qFatal("%s", reason);
}

QByteArray contents(const QString &path)
{
    QFile input(path);
    check(input.open(QIODevice::ReadOnly), "cannot open test file");
    return input.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile output(path);
    check(output.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot create test file");
    check(output.write(bytes) == bytes.size(), "cannot write test file");
}

void key(QWidget *widget, Qt::Key code)
{
    QKeyEvent down(QEvent::KeyPress, code, Qt::NoModifier);
    QApplication::sendEvent(widget, &down);
    QKeyEvent up(QEvent::KeyRelease, code, Qt::NoModifier);
    QApplication::sendEvent(widget, &up);
    QApplication::processEvents();
}

void settleTitles()
{
    QEventLoop loop;
    QTimer::singleShot(160, &loop, &QEventLoop::quit);
    loop.exec();
}

void storage(const QString &directory)
{
    const QString source = directory + "/my notes.txt";
    const QString target = directory + "/second.txt";
    const QString sidecar = source + ".bookmarks";
    QString error;
    BookmarkFile doc;
    check(doc.openManuscript(source, &error), "opening a manuscript without bookmarks failed");
    check(doc.lastCategory() == "General" && doc.nextName("Revisit") == "1", "initial defaults");
    check(!QFile::exists(sidecar), "reading created an empty sidecar");
    check(doc.add("Revisit", "", 200, &error), "first automatic name");
    check(doc.add("Revisit", "", 200, &error), "two bookmarks on one line are distinct");
    check(doc.add("引文", "A \"quote\" × 🦋", 3, &error), "UTF-8 bookmark with quotes");
    check(doc.add("引文", "", 9, &error), "independent category counter");
    check(!doc.add("Revisit", "1", 300, &error), "duplicate category/name accepted");
    const QByteArray bytes = contents(sidecar);
    check(!bytes.startsWith("\xEF\xBB\xBF") && bytes.contains(QStringLiteral("引文").toUtf8()),
          "sidecar text must be original UTF-8 without a BOM");
    check(QJsonDocument::fromJson(bytes).object().value("version").toInt() == 1, "version marker");
    BookmarkFile restored;
    check(restored.openManuscript(source, &error), "could not reopen bookmark file");
    check(restored.entries().size() == 4 && restored.entries().at(0).line == 200,
          "bookmark physical line changed during persistence");
    check(restored.lastCategory() == "引文" && restored.nextName("Revisit") == "3" &&
          restored.nextName("引文") == "2", "names/last category did not survive reopening");
    check(restored.renameBookmark("引文", "1", "Notes", "seven", &error), "move and rename");
    check(restored.renameCategory("引文", "引用", &error), "rename category");
    check(restored.remove("Revisit", "2", &error) && restored.nextName("Revisit") == "3",
          "deleted name was reused");
    check(restored.add("Revisit", "10", 22, &error) && restored.nextName("Revisit") == "11",
          "explicit numeric bookmark collided with a future automatic name");
    check(!restored.renameCategory("引用", "Revisit", &error), "category rename overwrote destination");

    const QByteArray beforeCopy = contents(sidecar);
    check(restored.saveAs(target, &error) && !restored.hasPending(), "failed to save bookmark copy");
    check(contents(sidecar) == beforeCopy && contents(target + ".bookmarks") == beforeCopy,
          "Save As did not preserve both pairs");
    check(restored.add("New copy", "", 12, &error) && contents(sidecar) == beforeCopy,
          "Save As still targets the source file");

    // doc retains the original snapshot while another instance changed that path.
    check(!doc.add("Rejected", "", 10, &error) && contents(sidecar) == beforeCopy,
          "an outdated writer overwrote an external change");
    check(doc.entries().size() == 4, "unsaved edit applied after an external conflict");
    check(doc.openManuscript(source, &error) && doc.add("After reload", "", 10, &error),
          "reloading did not permit a subsequent checked save");
    check(!doc.add("", "name", 3, &error) && !doc.add("Cat", "", 0, &error),
          "invalid line/category accepted");
    check(doc.remove("Revisit", "", &error) && doc.nextName("Revisit") == "11",
          "whole-category delete lost sequence or failed");
}

void pendingAndConflicts(const QString &directory)
{
    const QString path = directory + "/first save.txt";
    const QString target = directory + "/existing.txt";
    const QString sidecar = target + ".bookmarks";
    QString error;
    BookmarkFile pending;
    check(pending.openManuscript({}, &error) && pending.add("Desk", "", 7, &error) &&
          pending.hasPending(), "untitled bookmarks were not retained");
    check(!pending.flush(&error) && pending.hasPending(), "untitled bookmarks silently lost");
    check(pending.saveAs(path, &error) && !pending.hasPending(), "first Save As of untitled book");
    const QByteArray source = contents(path + ".bookmarks");
    BookmarkFile targetDoc;
    check(targetDoc.openManuscript(target, &error) && targetDoc.add("External", "Other", 99, &error),
          "test fixture could not write external sidecar");
    const QByteArray external = contents(sidecar);
    check(!pending.saveAs(target, &error) && pending.hasPending() &&
          pending.manuscriptPath() == target, "destination conflict left bookmarks pointing to source");
    check(!pending.flush(&error) && contents(sidecar) == external, "retry overwrote destination");
    check(!pending.add("Rejected", "", 5, &error) && contents(path + ".bookmarks") == source,
          "conflicting Save As allowed a write to the previous path");
    check(QFile::remove(sidecar) && pending.flush(&error) && !pending.hasPending() &&
          contents(sidecar) == source, "cleared destination conflict did not recover");
    BookmarkFile empty;
    check(empty.openManuscript({}, &error) && empty.saveAs(target, &error) &&
          empty.entries().size() == 1, "empty source overwrote existing target bookmarks");

    const QString corrupt = directory + "/not valid.txt";
    writeFile(corrupt + ".bookmarks", "malformed");
    check(!empty.openManuscript(corrupt, &error) && !empty.add("Oops", "", 1, &error) &&
          contents(corrupt + ".bookmarks") == "malformed", "malformed sidecar overwritten");
    const QByteArray badLine = R"({"version":1,"bookmarks":[{"category":"a","name":"b","line":1.5}]})";
    writeFile(corrupt + ".bookmarks", badLine);
    check(!empty.openManuscript(corrupt, &error), "fractional physical line accepted");
}

void dialogs(const QString &directory)
{
    QWidget window;
    window.resize(800, 500);
    window.show();
    QStringList titles{"One", "Manuscript title", "Haiku #1", "Four"};
    int line = 2;
    int destination = 0;
    int titleQueries = 0;
    Bookmarks marks(&window, [&] { return line; }, [&] { return int(titles.size()); },
                    [&](int where) { ++titleQueries; return titles.at(where - 1); },
                    [&](int where) { destination = where; });
    QString error;
    const QString path = directory + "/interface.txt";
    check(marks.openManuscript(path, &error), "browser open");
    marks.addCurrent();
    auto *form = window.findChild<QDialog *>("bookmarkEditWindow");
    check(form && form->isVisible() && !form->isModal(), "add window is missing or modal");
    auto *category = form->findChild<QComboBox *>("bookmarkCategory");
    auto *name = form->findChild<QLineEdit *>("bookmarkName");
    const int available = qMin(window.width(), window.screen()->availableGeometry().width() * 9 / 10);
    check(form->width() >= qMin(600, available) && category->width() >= form->width() * 2 / 3,
          "add bookmark must give most of its wide form to the category control");
    category->setEditText("Revise");
    check(name->placeholderText() == "1", "initial proposed name");
    key(category->lineEdit(), Qt::Key_Return);
    check(!form->isVisible() && QFile::exists(path + ".bookmarks"), "quick Add with Enter failed");

    marks.showWindow();
    auto *browser = window.findChild<QDialog *>("bookmarkWindow");
    auto *tree = browser->findChild<QTreeWidget *>("bookmarkResults");
    auto *filter = browser->findChild<QLineEdit *>("bookmarkFilter");
    auto *view = browser->findChild<QComboBox *>("bookmarkViewMode");
    check(!browser->isModal() && tree->topLevelItemCount() == 1 &&
          tree->topLevelItem(0)->childCount() == 0 && tree->topLevelItem(0)->text(0) == "Revise",
          "a category with one bookmark must be a direct destination");
    key(filter, Qt::Key_Return);
    check(destination == 2 && !browser->isVisible(), "Enter from filter did not navigate");

    titleQueries = 0;
    marks.refresh();
    settleTitles();
    check(titleQueries == 0, "the hidden browser fetched manuscript titles");
    line = 3;
    marks.addCurrent();
    check(category->currentText() == "Revise" && name->placeholderText() == "2",
          "last category and next name missing");
    key(category->lineEdit(), Qt::Key_Return);
    marks.showWindow();
    check(tree->topLevelItem(0)->childCount() == 2, "second bookmark did not expand the category");
    filter->setText("HAIKU");
    check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 1 &&
          tree->currentItem()->text(0) == "2", "filter did not select the matching named item");
    view->setCurrentIndex(1);
    check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->text(1) == "2",
          "flat filtered view lost the bookmark name");
    key(filter, Qt::Key_Return);
    check(destination == 3, "flat view did not jump to stored line");

    // An inserted line changes the title at the same physical line.
    titles.prepend("Inserted");
    marks.showWindow();
    filter->setText("Manuscript title");
    check(tree->currentItem() && tree->currentItem()->text(2) == "3", "bookmark followed the original text");
    titles[2] = "Revised title";
    marks.refresh();
    settleTitles();
    check(tree->topLevelItemCount() == 0, "title filter didn't follow changed manuscript data");
    filter->setText("Revised");
    tree->setFocus();
    key(tree, Qt::Key_F2);
    check(form->isVisible() && name->text() == "2", "keyboard rename did not edit selected entry");
    name->setText("Return");
    key(name, Qt::Key_Return);
    check(tree->currentItem()->text(1) == "Return", "bookmark rename was not displayed");
    titles = {"Only one line"};
    filter->setText("Return");
    marks.refresh();
    settleTitles();
    destination = 0;
    key(filter, Qt::Key_Return);
    check(destination == 0 && browser->isVisible(), "missing line silently jumped elsewhere");
    key(filter, Qt::Key_Escape);
    check(!browser->isVisible(), "Escape did not close bookmark window");

    window.resize(340, 350);
    line = 1;
    marks.addCurrent();
    QApplication::processEvents();
    check(form->width() <= window.width() && category->lineEdit()->width() > form->width() / 2,
          "the same editable category fits inside a narrow manuscript tile");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "could not create temporary test directory");
    storage(temporary.path());
    pendingAndConflicts(temporary.path());
    dialogs(temporary.path());
    return 0;
}
