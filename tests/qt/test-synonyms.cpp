#include "synonymfile.h"
#include "synonymmatch.h"
#include "synonympanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTextBlock>
#include <QTemporaryDir>
#include <QThread>
#include <QElapsedTimer>
#include <cstdio>

namespace {
void check(bool ok, const char *why)
{
    if (!ok) qFatal("Synonym test: %s", why);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "cannot read test file");
    return file.readAll();
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "cannot write test fixture");
}

void settle()
{
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 5000) {
        settle();
        QThread::msleep(3);
    }
    return ready();
}

void key(QWidget *target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = {})
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(target, &release);
    settle();
}

void grammar()
{
    QStringList aliases;
    QString error;
    const QString line = QStringLiteral(" \"Dr. Ada, DDS\" , L’étincelle , \"She said \"\"yes\"\"\" , 怪奇文学 ");
    check(SynonymFile::parse(line, aliases, &error), "valid quoted aliases");
    check(aliases == QStringList({"Dr. Ada, DDS", "L’étincelle", "She said \"yes\"", "怪奇文学"}), "decoded aliases");
    check(SynonymFile::parse(SynonymFile::format(aliases), aliases) && aliases.at(2) == "She said \"yes\"",
          "escaped quotes round trip");
    const QStringList previous = aliases;
    for (const auto &bad : QStringList{"", " , ", "alpha,", "alpha,,beta", "\"unfinished", "\"one\"beta",
                                       "tw\"o", "a\nb", "a\rb", QString(QChar::Null)})
        check(!SynonymFile::parse(bad, aliases, &error) && aliases == previous, "invalid edits cannot replace data");
}

void persistence(const QString &directory)
{
    const QString first = directory + "/first.txt", second = directory + "/second.txt";
    const QString suffix = ".characters";
    SynonymFile chars(suffix);
    check(chars.openManuscript(first) && !QFile::exists(first + suffix) && !chars.hasPending(), "open has no side effects");
    check(chars.add("Zebra, Zed") && chars.add("Ada, \"Ada, DDS\""), "adding names");
    check(chars.groups().first().aliases.first() == "Ada" && read(first + suffix) == "Ada, \"Ada, DDS\"\nZebra, Zed\n",
          "UTF-8 ordered lines");
    SynonymFile restored(suffix);
    check(restored.openManuscript(first) && restored.groups() == chars.groups(), "companion reopened");
    check(restored.edit(0, "Åda, Dr. Ada") && restored.groups().size() == 2, "Unicode rename");
    check(!chars.remove(1) && chars.groups().size() == 2, "external edits protected");
    const QByteArray source = read(first + suffix);
    check(!source.startsWith("\xEF\xBB\xBF") && source.contains(QStringLiteral("Åda").toUtf8()), "UTF-8 without BOM");
    check(restored.saveAs(second) && source == read(second + suffix), "Save As copies bytes");
    check(restored.remove(0) && source == read(first + suffix) && source != read(second + suffix), "Save As isolation");

    SynonymFile untitled(suffix);
    check(untitled.add("New, Latest") && untitled.hasPending(), "untitled list retained in memory");
    check(!untitled.flush(), "untitled file requires a name");
    const QString occupied = directory + "/occupied.txt";
    write(occupied + suffix, "someone else's companion\n");
    check(!untitled.saveAs(occupied) && untitled.hasPending() && read(occupied + suffix) == "someone else's companion\n",
          "colliding companion preserved");
    const QString next = directory + "/elsewhere.txt";
    check(untitled.saveAs(next) && !untitled.hasPending() && read(next + suffix) == "New, Latest\n",
          "pending Save As recovers under a different name");
    const QString emptySource = directory + "/empty.txt";
    check(untitled.openManuscript(emptySource) && untitled.saveAs(occupied) &&
          untitled.groups().first().aliases.first() == "someone else's companion", "empty source adopts existing destination");

    const QString broken = directory + "/broken.txt", copy = directory + "/copy.txt";
    write(broken + suffix, "\"unclosed\n");
    check(!untitled.openManuscript(broken) && !untitled.add("Replacement") && read(broken + suffix) == "\"unclosed\n",
          "a malformed sidecar remains intact");
    check(!untitled.saveAs(copy) && read(copy + suffix) == read(broken + suffix), "malformed original bytes survive Save As");
    write(broken + suffix, QByteArray("x\xff", 2));
    check(!untitled.openManuscript(broken), "invalid UTF-8 detected");
    write(broken + suffix, QByteArray("\xEF\xBB\xBF") + "Zebra,Zed\r\n\r\nAda, \"Dr. Ada, DDS\"\r\n");
    check(untitled.openManuscript(broken) && untitled.groups().first().aliases.first() == "Ada", "manual UTF-8 CSV edits reload");
    check(untitled.remove(1) && read(broken + suffix) == "Ada, \"Dr. Ada, DDS\"\n", "successful edit emits UTF-8 LF");
    check(untitled.add("Z, \",no-complete, quoted alias\"", true) &&
          untitled.edit(0, "Ada, Dr. Ada", true) && untitled.openManuscript(broken) &&
          untitled.groups().first().excludeFromAutocomplete && untitled.groups().last().excludeFromAutocomplete &&
          untitled.groups().last().aliases.last() == ",no-complete, quoted alias",
          "exclusion is portable and cannot collide with a quoted legacy alias");
    check(untitled.edit(0, "Ada, Addie") && untitled.groups().first().excludeFromAutocomplete &&
          untitled.edit(0, "Ada, Addie", false) && !untitled.groups().first().excludeFromAutocomplete &&
          read(broken + suffix).startsWith("Ada, Addie\n"), "renaming preserves exclusion; unchecking restores plain CSV");
}

void pane(const QString &dir)
{
    QWidget window;
    QHBoxLayout layout(&window);
    QPlainTextEdit editor;
    SynonymPanel panel(editor.document());
    layout.addWidget(&panel);
    layout.addWidget(&editor);
    const auto path = dir + "/writing.txt";
    write(path + ".characters", "Zorbo Marmalade, Zed, \"Dr. Ada, DDS\", A.A., O'Neil, Violet Flame\n"
                                "Other Name, Alias, Violet Lamp\n");
    write(path + ".recurring", "Winter, Wintertime\n");
    check(panel.openManuscript(path) && panel.allAliases().contains("Zorbo Marmalade") &&
          !panel.allAliases().contains("Zorbo") && panel.allAliases().contains("Wintertime"), "complete cross-list spelling vocabulary");
    editor.setPlainText("Crumb 1: One: Zed szed ZEDDISH. Zorbo Marmalade, O’Neil and O'Neil.\n"
                        "Crumb 2: Two: 🦋 DR. ADA, DDS then A.A..");
    auto cursor = editor.textCursor();
    cursor.setPosition(editor.toPlainText().indexOf("then"));
    editor.setTextCursor(cursor);
    panel.insertSynonym = [&](const QString &s) { editor.insertPlainText(s); editor.setFocus(); };
    panel.returnToEditor = [&] { editor.setFocus(); };
    int jumped = -1;
    QString selected;
    panel.jumpToMatch = [&](int start, int end) {
        auto c = editor.textCursor();
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
        editor.setTextCursor(c);
        selected = c.selectedText();
        jumped = start;
        editor.setFocus();
    };
    int changes = 0;
    panel.onChanged = [&] { ++changes; };
    window.show();
    window.activateWindow();
    settle();
    auto *search = panel.findChild<QLineEdit *>("charactersSearch");
    auto *list = panel.findChild<QListView *>("charactersList");
    auto *things = panel.findChild<QListView *>("recurringList");
    auto *thingSearch = panel.findChild<QLineEdit *>("recurringSearch");
    panel.focusCharacters();
    check(search->hasFocus() && search->placeholderText() == "Search characters", "shortcut focuses character search");
    search->setText("vIOleT");
    check(list->model()->rowCount() == 2 && list->currentIndex().row() == 0, "first alternate name matches in place");
    key(search, Qt::Key_Up);
    check(search->hasFocus() && list->currentIndex().row() == 1, "previous match wraps without leaving the field");
    key(search, Qt::Key_Down);
    check(list->currentIndex().row() == 0, "next match wraps");
    search->setText("absent");
    key(search, Qt::Key_Return, Qt::ControlModifier);
    check(!list->currentIndex().isValid() && search->text() == "absent" && list->model()->rowCount() == 2 &&
          !editor.toPlainText().contains("Other Name"), "a missing match leaves the search and manuscript intact");
    search->setText("ada, dd");
    check(list->currentIndex().row() == 1, "search handles punctuation within a quoted alias");
    key(search, Qt::Key_Return);
    auto *view = panel.findChild<QTableView *>("synonymMatches");
    check(search->text().isEmpty() && view && waitFor([&] { return view->model()->rowCount() == 6; }),
          "Enter clears the search and finds all literal occurrences, whole names and curly apostrophes");
    auto row = view->model()->index(4, 1);
    check(row.data().toString() == "DR. ADA, DDS" && row.siblingAtColumn(0).data().toInt() == 2,
          "ordered, case-preserving results with physical line number");
    view->setCurrentIndex(row);
    key(view, Qt::Key_Return);
    check(jumped == editor.toPlainText().indexOf("DR. ADA, DDS") && selected == "DR. ADA, DDS",
          "activated occurrence uses exact UTF-16 offsets");
    panel.focusCharacters();
    search->setText("zed");
    key(search, Qt::Key_Return, Qt::ControlModifier);
    auto *choices = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    check(choices && choices->actions().size() == 6 && choices->actions()[0]->text() == "Zorbo Marmalade" &&
          choices->actions()[1]->text() == "Zed" && choices->actions()[2]->text() == "Dr. Ada, DDS",
          "Ctrl+Enter lists each saved spelling in original order");
    key(choices, Qt::Key_Down);
    key(choices, Qt::Key_Return);
    check(search->text().isEmpty() && editor.toPlainText().contains("🦋 Zed then") && editor.hasFocus(),
          "an alternate spelling replaces the saved selection and returns focus");

    panel.focusRecurring();
    check(thingSearch->hasFocus() && thingSearch->placeholderText() == "Search recurring", "shortcut focuses recurring search");
    key(thingSearch, Qt::Key_Insert);
    auto *form = panel.findChild<QDialog *>("recurringEditDialog");
    check(form && form->isVisible(), "keyboard add opens nonmodal editor");
    auto *field = form->findChild<QLineEdit *>("synonymText");
    auto *excluded = form->findChild<QCheckBox *>("excludeFromAutocomplete");
    check(excluded && !excluded->isChecked(), "new groups default to offering completions");
    field->setText("Beetle, \"The Beetle, Esquire\"");
    key(field, Qt::Key_Return);
    check(panel.allAliases().contains("The Beetle, Esquire") && read(path + ".recurring").startsWith("Beetle,"),
          "keyboard add saves sorted groups");
    key(things, Qt::Key_F2);
    form = panel.findChild<QDialog *>("recurringEditDialog");
    field = form->findChild<QLineEdit *>("synonymText");
    excluded = form->findChild<QCheckBox *>("excludeFromAutocomplete");
    key(excluded, Qt::Key_Space);
    field->setText("Bug, Beetle");
    key(field, Qt::Key_Return);
    check(panel.allAliases().contains("Bug") && !panel.allAliases().contains("The Beetle, Esquire") &&
          read(path + ".recurring").startsWith(",no-complete, Bug, Beetle\n"),
          "keyboard exclusion and rename keep both names in the spelling vocabulary");
    key(things, Qt::Key_Delete);
    auto *question = panel.findChild<QMessageBox *>();
    check(question && question->isVisible(), "deleting asks before removing a group");
    auto *yes = question->button(QMessageBox::Cancel);
    key(yes, Qt::Key_Escape);
    check(panel.allAliases().contains("Bug"), "Escape keeps the group");
    key(things, Qt::Key_Delete);
    question = panel.findChild<QMessageBox *>();
    for (auto *button : question->buttons())
        if (question->buttonRole(button) == QMessageBox::DestructiveRole) { button->click(); break; }
    settle();
    check(!panel.allAliases().contains("Bug") && changes == 3, "confirmed delete updates files and vocabulary");
    write(path + ".recurring", "Lightning, Flash\n");
    key(things, Qt::Key_F5);
    check(panel.allAliases().contains("Flash") && !panel.allAliases().contains("Winter"), "F5 refreshes manually edited file");

    search->setText("zed");
    panel.focusCharacters();
    key(search, Qt::Key_Return);
    check(waitFor([&] { return view->model()->rowCount() == 6; }), "search reopened after document edit");
    editor.setPlainText("Crumb 1: Fresh: Zed");
    check(waitFor([&] { return view->model()->rowCount() == 1; }) &&
          view->model()->index(0, 1).data() == QStringLiteral("Zed"), "live result offsets refresh after document edit");
    key(view, Qt::Key_Escape);
}

void overlaps(const QString &dir)
{
    QPlainTextEdit editor;
    SynonymPanel panel(editor.document());
    const auto path = dir + "/overlaps";
    write(path + ".characters", "A A, A, a, Goose\n");
    write(path + ".recurring", "Inspector Goose\n");
    editor.setPlainText("A A A. Inspector Goose fined that goose.");
    panel.openManuscript(path);
    panel.show();
    auto *search = panel.findChild<QLineEdit *>("charactersSearch");
    key(search, Qt::Key_Return);
    auto *view = panel.findChild<QTableView *>("synonymMatches");
    check(waitFor([&] { return view->model()->rowCount() == 3; }), "nested hits are suppressed across lists");
    check(view->model()->index(0, 1).data() == "A A" && view->model()->index(1, 1).data() == "A A" &&
          view->model()->index(2, 1).data() == "goose", "crossing spans and a separate short mention are retained");
    const auto passageA = view->model()->index(0, 2);
    const auto passageB = view->model()->index(1, 2);
    check(passageA.data() == passageB.data() && passageA.data(Qt::UserRole + 2).toPoint() == QPoint(0, 3) &&
          passageB.data(Qt::UserRole + 2).toPoint() == QPoint(2, 3) && view->itemDelegateForColumn(2),
          "two identical passages identify different exact spans for bold type");
    // A split above an existing result updates its line and UTF-16 positions.
    auto c = editor.textCursor();
    c.setPosition(0);
    c.insertText("Goose\n");
    check(waitFor([&] { return view->model()->rowCount() == 4; }) && view->model()->index(3, 0).data() == 2 &&
          view->model()->index(3, 1).data(Qt::UserRole) == editor.toPlainText().lastIndexOf("goose"),
          "live occurrence table relocates existing rows");
    c.setPosition(0);
    c.setPosition(6, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    check(waitFor([&] { return view->model()->rowCount() == 3; }) && view->model()->index(2, 0).data() == 1,
          "merging away the inserted line updates the open table");
}

void matcher()
{
    QPlainTextEdit editor;
    SynonymMatcher matches(editor.document());
    matches.setGroups({{{"Inspector Goose", "Goose", "O'Neil"}}, {{"Shared", "Alias"}}},
                      {{{"Understudy", "Alias"}}, {{"His Feathered Excellency", "O’Neil"}}});
    editor.setPlainText("🪶 Inspector Goose saw a goose.\nO’NEIL met an alias, and Aliasing left.");
    const auto first = matches.mentions(editor.document()->begin());
    const auto second = matches.mentions(editor.document()->lastBlock());
    check(first.size() == 2 && first.first().start == 3 && first.first().end == 18 &&
          first.last().groups == QList<int>{0}, "nested alias is suppressed and supplementary offsets survive");
    check(second.size() == 2 && second.first().groups == QList<int>({0, 3}) &&
          second.last().groups == QList<int>({1, 2}), "whole words, apostrophes, and ambiguous groups");
    check(matches.mentionAt(first.first().end)->start == first.first().start &&
          matches.mentionAt(first.first().end + 1) == std::nullopt, "at and immediately after mentions");
    const auto previous = matches.neighbor(second.first(), 0, true);
    check(previous && previous->start == first.last().start && !matches.neighbor(first.first(), 0, true) &&
          matches.neighbor(first.last(), 0, false)->start == second.first().start, "strict document-order navigation");

    auto c = editor.textCursor();
    c.setPosition(first.last().start);
    c.setPosition(first.last().end, QTextCursor::KeepAnchor);
    c.insertText("duck");
    check(matches.mentions(editor.document()->begin()).size() == 1 &&
          matches.mentions(editor.document()->lastBlock()).first().start == second.first().start - 1,
          "only the edited block changes; later offsets follow the text");
    editor.undo();
    check(matches.mentions(editor.document()->begin()).size() == 2, "cached matches refresh on undo");

    matches.setGroups({{{"X", "the lunar auditor", "O'Neil"}}}, {{{"O’Neil", "lunar audience"}}});
    editor.setPlainText("the lunar aud");
    auto candidates = matches.completions(13, true, true, 3);
    check(candidates.size() == 1 && candidates.first().alias == "the lunar auditor" &&
          candidates.first().start == 0 && candidates.first().suffix == "itor", "longest multiword prefix wins");
    candidates = matches.completions(13, false, true, 3);
    check(candidates.size() == 1 && candidates.first().start == 4 &&
          candidates.first().suffix == "ience", "list filters apply before choosing the longest prefix");
    editor.setPlainText("O’N");
    candidates = matches.completions(3, true, true, 3);
    check(candidates.size() == 1 && candidates.first().suffix == "eil", "equivalent spellings complete once");
    check(matches.completions(3, true, true, 4).isEmpty() && matches.completions(2, true, true, 1).isEmpty(),
          "minimum prefix and inside-word safeguards");
    matches.setGroups({{{"Greenfinch", "Greengage", "Green goose"}}, {{"Green room"}}}, {});
    editor.setPlainText("gre");
    check(matches.completions(3, true, false, 3, 2).size() == 2 &&
          matches.completions(3, true, false, 3).size() == 4,
          "the quiet hint detects ambiguity early while the chooser lists every candidate");
    matches.setGroups({{{"Rocket", "Comet"}, true}}, {{{"Rocket", "Copernicus"}}}, {"Comet", "Rockslide"});
    editor.setPlainText("Comet flew by a Rockslide. Another comet.\nCo");
    const auto mentions = matches.mentions(editor.document()->begin());
    check(mentions.size() == 3 && mentions.first().groups == QList<int>({0, 2}) &&
          matches.neighbor(mentions.first(), 2, false)->start == editor.toPlainText().lastIndexOf("comet") &&
          matches.groups().at(mentions.at(1).groups.first()).source == NameSource::Genre,
          "genre and excluded names keep distinct identities for marking and navigation");
    candidates = matches.completions(editor.toPlainText().size(), true, true);
    check(candidates.size() == 1 && candidates.first().alias == "Copernicus",
          "neither excluded groups nor manuscript genres supply completions");
    editor.setPlainText("Roc");
    candidates = matches.completions(3, true, true);
    check(candidates.size() == 1 && candidates.first().alias == "Rocket" &&
          matches.completions(3, true, false).isEmpty(), "an eligible duplicate alias can still complete");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    check(dir.isValid(), "temporary directory");
    grammar();
    persistence(dir.path());
    pane(dir.path());
    overlaps(dir.path());
    matcher();
    std::puts("Synonym groups, companion files, keyboard operations and occurrence search passed.");
}
