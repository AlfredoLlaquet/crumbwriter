#include "bookmarkfile.h"
#include "bookmarks.h"

#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QKeyEvent>
#include <QtGui/QScreen>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>
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
    check(QJsonDocument::fromJson(bytes).object().value("version").toInt() == 2, "version marker");
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

void liveAnchors(const QString &directory)
{
    QTextDocument document;
    document.setPlainText("one\nfirst\nbookmarked paragraph\nsuccessor\nlast\n");
    BookmarkFile file(&document);
    QObject::connect(&document, &QTextDocument::undoCommandAdded, [&] { file.noteUndoCommand(); });
    QObject::connect(&document, &QTextDocument::contentsChange, [&](int pos, int removed, int added) {
        if (removed || added) file.trackEdit(pos, added);
    });
    const QString path = directory + "/working.txt";
    QString error;
    check(file.openManuscript(path) && file.add("Scene", "Door", 3), "create tracked bookmark");
    check(file.lineCount() == 5 && !file.add("Scene", "Virtual", 6), "final LF became a file line");

    auto targetText = [&] { return document.findBlockByNumber(file.entries().first().line - 1).text(); };
    QTextCursor edit(document.findBlockByNumber(2));
    edit.insertText("inserted at original start\n");
    check(file.entries().first().line == 4 && targetText() == "bookmarked paragraph",
          "inserting at the anchor left it on the new paragraph");
    edit = QTextCursor(document.findBlockByNumber(3));
    edit.setPosition(edit.position() + 5);
    edit.insertText("\n");
    check(file.entries().first().line == 4 && targetText() == "bookm", "split lost bookmarked beginning");
    document.undo();
    check(file.entries().first().line == 4 && targetText() == "bookmarked paragraph", "undo split");

    // Delete two complete lines, including the target, within one edit block.
    edit = QTextCursor(document.findBlockByNumber(2));
    edit.setPosition(document.findBlockByNumber(4).position(), QTextCursor::KeepAnchor);
    edit.beginEditBlock();
    edit.removeSelectedText();
    edit.endEditBlock();
    check(file.entries().first().line == 3 && targetText() == "successor", "deleted target did not fall forward");
    document.undo();
    check(file.entries().first().line == 4 && targetText() == "bookmarked paragraph",
          "undo deletion did not recover deleted target");
    document.redo();
    check(file.entries().first().line == 3 && targetText() == "successor", "redo did not reapply fallback");
    document.undo();
    edit = QTextCursor(&document);
    edit.insertText("new branch\n");
    check(file.entries().first().line == 5 && targetText() == "bookmarked paragraph",
          "new undo branch inherited obsolete destinations");
    check(file.hasPending() && file.flush(&error), "could not publish relocated anchors");
    const QByteArray source = contents(path + ".bookmarks");
    const auto persisted = QJsonDocument::fromJson(source).object().value("bookmarks").toArray().first().toObject();
    check(persisted.value("line").toInt() == 5 && persisted.value("context").toObject().size() == 3,
          "sidecar omitted current position or neighbors");

    // A bookmarked EOF deletion and a cleared file both retain a valid destination.
    check(file.add("Scene", "End", file.lineCount()), "bookmark at last real line");
    edit = QTextCursor(document.findBlockByNumber(file.lineCount() - 1));
    edit.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    check(file.entries().last().line == file.lineCount() &&
          document.findBlockByNumber(file.lineCount() - 1).text() == "successor", "EOF deletion chose Qt sentinel");
    edit.select(QTextCursor::Document);
    edit.removeSelectedText();
    check(file.lineCount() == 1 && file.entries().first().line == 1 && file.entries().last().line == 1,
          "empty file has no bookmark destination");
    document.undo();
    check(targetText() == "bookmarked paragraph", "undo clearing lost the manuscript target");
    const QString copy = directory + "/moved bookmarks copy.txt";
    check(file.saveAs(copy) && contents(copy + ".bookmarks") != source, "Save As omitted pending anchors");
    // Adding the End bookmark was intentionally saved in the source already.
    const auto old = QJsonDocument::fromJson(contents(path + ".bookmarks")).object().value("bookmarks").toArray();
    check(old.last().toObject().value("line").toInt() == 7, "Save As overwrote source bookmark positions");
}

void externalEdits(const QString &directory)
{
    const QString path = directory + "/reopen.txt";
    const QString sidecar = path + ".bookmarks";
    const QString base = "heading\nwest\nrepeat\nsouth\neast\nrepeat\nnorth\nlast\n";
    QTextDocument document;
    document.setPlainText(base);
    BookmarkFile file(&document);
    check(file.openManuscript(path) && file.add("Scenes", "First", 3) &&
          file.add("Scenes", "Second", 6), "prepare external-edit bookmarks");

    document.setPlainText("inserted\nheading\neast\nrepeat\nnorth\nwest\nrepeat\nsouth\nlast\n");
    check(file.openManuscript(path) && file.entries()[0].line == 7 && file.entries()[1].line == 4,
          "external moves confused duplicate paragraphs with different neighbors");
    document.setPlainText("inserted\nheading\neast\nrepeat\nnorth\nlast\n");
    check(file.openManuscript(path) && file.entries()[0].line == 4,
          "a surviving exact duplicate should win when its former neighbors have vanished");

    document.setPlainText("preamble\nleft\noriginal\nright\ntail");
    const QString edited = directory + "/externally edited.txt";
    check(file.openManuscript(edited) && file.add("Scenes", "Revised", 3), "prepare rewritten target");
    document.setPlainText("added\npreamble\nleft\ncompletely rewritten\nright\ntail");
    check(file.openManuscript(edited) && file.entries().first().line == 4,
          "a rewritten target should stay between its unchanged neighbors");
    document.setPlainText("added\npreamble\nleft\nright\ntail");
    check(file.openManuscript(edited) && file.entries().first().line == 4,
          "a missing target should transfer to its old successor");
    document.setPlainText("entirely different");
    check(file.openManuscript(edited) && file.entries().first().line == 1, "unrecognizable manuscript fallback");
    document.setPlainText("no\nfamiliar\nlines\nleft\nanywhere");
    check(file.openManuscript(edited) && file.entries().first().line == 1,
          "EOF by itself overrode the stored physical line in an unrecognizable manuscript");
    const QString ties = directory + "/repeated lines.txt";
    document.setPlainText("a\nb\nwest\nsame\neast");
    check(file.openManuscript(ties) && file.add("Pins", "Repeat", 4), "prepare ambiguous duplicate");
    document.setPlainText("x\ny\nsame\nz\nsame\nw");
    check(file.openManuscript(ties) && file.entries().first().line == 3,
          "equally near duplicates without surviving neighbors need a deterministic destination");

    // Legacy data still carries category counters (including previously deleted names).
    writeFile(sidecar, R"({"version":1,"lastCategory":"Scenes","counters":{"Scenes":9},"bookmarks":[{"category":"Scenes","name":"3","line":200}]})");
    document.setPlainText("first\n\nlast\n");
    check(file.openManuscript(path) && file.entries().first().line == 3 && file.nextName("Scenes") == "10",
          "legacy line/counter migration");
    auto saved = QJsonDocument::fromJson(contents(sidecar)).object();
    check(saved.value("version").toInt() == 2 &&
          saved.value("bookmarks").toArray().first().toObject().value("context").toObject().contains("target"),
          "legacy bookmark did not gain persistent context");
    saved["version"] = 3;
    writeFile(sidecar, QJsonDocument(saved).toJson());
    check(!file.openManuscript(path) && !file.add("Corrupt", {}, 1), "unsupported future format overwritten");
}

void inFlightSave(const QString &directory)
{
    QTextDocument document;
    document.setPlainText("one\ntarget\ntail");
    BookmarkFile file(&document);
    QObject::connect(&document, &QTextDocument::undoCommandAdded, [&] { file.noteUndoCommand(); });
    QObject::connect(&document, &QTextDocument::contentsChange, [&](int pos, int removed, int added) {
        if (removed || added) file.trackEdit(pos, added);
    });
    auto prepend = [&] { QTextCursor(&document).insertText("more\n"); };
    auto saved = [](const QString &path) {
        return QJsonDocument::fromJson(contents(path + ".bookmarks")).object()["bookmarks"].toArray();
    };
    const QString source = directory + "/in flight.txt";
    const QString destination = directory + "/in flight copy.txt";
    check(file.openManuscript(source) && file.add("Desk", "Target", 2), "prepare concurrent save");
    prepend();
    file.captureForSave(); // Text at dispatch has the target on line 3.
    prepend();
    check(file.publishSave() && saved(source).first().toObject()["line"].toInt() == 3 &&
          file.entries().first().line == 4 && file.hasPending(), "live save mixed manuscript revisions");
    file.captureForSave(); // Save As dispatched with target on line 4.
    prepend();
    check(file.saveAs(destination) && file.publishSave() && file.hasPending() &&
          saved(source).first().toObject()["line"].toInt() == 3 &&
          saved(destination).first().toObject()["line"].toInt() == 4,
          "Save As copied anchors from an intervening edit or changed its source");
    file.captureForSave();
    check(file.publishSave() && !file.hasPending() && saved(destination).first().toObject()["line"].toInt() == 5,
          "next manuscript revision did not publish its anchors");
    prepend();
    file.captureForSave();
    check(file.renameBookmark("Desk", "Target", "Desk", "Renamed") && file.publishSave() &&
          saved(destination).first().toObject()["name"].toString() == "Renamed",
          "in-flight save resurrected an obsolete bookmark name");
    const auto before = contents(destination + ".bookmarks");
    prepend();
    file.captureForSave(); // A failed manuscript write never receives a publication callback.
    check(contents(destination + ".bookmarks") == before && file.hasPending(),
          "capturing an unsuccessful save touched the bookmark file");
}

void dialogs(const QString &directory)
{
    QWidget window;
    window.resize(800, 500);
    window.show();
    QTextDocument manuscript;
    manuscript.setPlainText("One\nManuscript title\nHaiku #1\nFour");
    int line = 2;
    int destination = 0;
    int titleQueries = 0;
    Bookmarks marks(&window, [&] { return line; }, &manuscript,
                    [&](int where) { ++titleQueries; return manuscript.findBlockByNumber(where - 1).text(); },
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

    QTextCursor cursor(&manuscript);
    cursor.insertText("Inserted\n");
    marks.showWindow();
    filter->setText("Manuscript title");
    check(tree->currentItem() && tree->currentItem()->text(2) == "3", "bookmark lost its original text");
    filter->setText("Haiku");
    check(tree->currentItem() && tree->currentItem()->text(2) == "4", "second bookmark did not move");
    cursor = QTextCursor(manuscript.findBlockByNumber(3));
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText("Revised title");
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
    cursor = QTextCursor(&manuscript);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    filter->setText("Return");
    marks.refresh();
    settleTitles();
    destination = 0;
    key(filter, Qt::Key_Return);
    check(destination == 1 && !browser->isVisible(), "deleted tail did not fall back to the last real line");
    marks.showWindow();
    key(filter, Qt::Key_Escape);
    check(!browser->isVisible(), "Escape did not close bookmark window");

    window.resize(340, 350);
    line = 1;
    marks.addCurrent();
    QApplication::processEvents();
    check(form->width() <= window.width() && category->lineEdit()->width() > form->width() / 2,
          "the same editable category fits inside a narrow manuscript tile");
    category->setEditText("Moving");
    QTextCursor(&manuscript).insertText("Before the form target\n");
    key(category->lineEdit(), Qt::Key_Return);
    marks.showWindow();
    filter->setText("Moving");
    check(tree->currentItem() && tree->currentItem()->text(2) == "2",
          "modeless Add Bookmark used the old position after an insertion");
    QTextCursor(&manuscript).insertText("Before the displayed destination\n");
    key(filter, Qt::Key_Return);
    check(destination == 3 && !browser->isVisible(), "Enter navigated to stale tree contents before refresh");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "could not create temporary test directory");
    storage(temporary.path());
    pendingAndConflicts(temporary.path());
    liveAnchors(temporary.path());
    externalEdits(temporary.path());
    inFlightSave(temporary.path());
    dialogs(temporary.path());
    return 0;
}
