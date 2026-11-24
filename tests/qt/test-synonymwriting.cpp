#include "manuscripteditor.h"
#include "synonymwriting.h"

#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <cstdio>

namespace {
void check(bool ok, const char *message)
{
    if (!ok) qFatal("Synonym writing: %s", message);
}

void key(QWidget &widget, int code, const QString &text = {})
{
    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
}

void type(ManuscriptEditor &editor, const QString &text)
{
    for (const auto ch : text) key(editor, ch.toUpper().unicode(), QString(ch));
}

void settle()
{
    QApplication::processEvents();
    QApplication::processEvents();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ManuscriptEditor editor;
    SynonymMatcher matcher(editor.document());
    SynonymWriting writing(&editor, &matcher);
    matcher.setGroups({{{"Inspector Goose", "the lunar auditor", "His Feathered Excellency"}},
                       {{"the lunar archive"}}}, {{{"Goose", "geese"}}, {{"Moonrise"}}});
    writing.listsChanged();
    QString status, notice;
    writing.onHint = [&](const QString &text) { status = text; };
    writing.onNotice = [&](const QString &text) { notice = text; };
    editor.resize(900, 400);
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    settle();
    const auto ghost = editor.findChild<QLabel *>("synonymCompletionHint");
    check(ghost, "completion preview exists");
    editor.loadText("A sentence.");
    type(editor, " Hi");
    check(!ghost->isVisible() && status.isEmpty(), "minimum of three typed characters");
    type(editor, "s Fea");
    check(ghost->isVisible() && ghost->text() == "thered Excellency", "quiet inline preview of a multiword name");
    check(editor.manuscriptText() == "A sentence. His Fea", "a preview never enters the manuscript");
    writing.complete();
    check(editor.manuscriptText() == "A sentence. His Feathered Excellency", "accept appends just the suffix");
    editor.undo();
    check(editor.manuscriptText() == "A sentence. His Fea", "whole completion is one undo step");
    check(!ghost->isVisible() && status.isEmpty(), "undo does not suggest");
    editor.redo();
    check(editor.manuscriptText().endsWith("His Feathered Excellency"), "redo restores full completion");

    editor.loadText("A sentence.");
    type(editor, " the lunar a");
    check(!ghost->isVisible() && status.isEmpty(), "ambiguous prefixes stay silent");
    writing.complete();
    auto *list = editor.findChild<QListView *>("synonymChoiceList");
    check(list && list->isVisible() && list->model()->rowCount() == 2, "explicit list holds both choices");
    key(*list, Qt::Key_Escape);
    check(!list->isVisible() && editor.manuscriptText().endsWith("the lunar a"), "cancel leaves text alone");
    writing.complete();
    key(*list, Qt::Key_Down);
    const auto selected = list->currentIndex().data().toString();
    key(*list, Qt::Key_Return);
    check(!list->isVisible() && editor.manuscriptText() == "A sentence. " + selected,
          "arrows and Enter accept the selected name");

    editor.loadText("A sentence.");
    type(editor, " Ins");
    key(editor, Qt::Key_Escape);
    check(!ghost->isVisible() && status.isEmpty(), "Escape dismisses a suggestion");
    editor.loadText("A sentence.");
    editor.insertLiteral(" His Fea");
    check(!ghost->isVisible() && status.isEmpty(), "pasting a prefix stays quiet");
    editor.loadText("A sentence. It continues.");
    editor.jumpTo(10, false);
    type(editor, " His Fea");
    check(!ghost->isVisible() && status.contains("His Feathered Excellency"), "text after the caret stays visible");
    writing.complete();
    check(editor.manuscriptText() == "A sentence His Feathered Excellency. It continues.", "middle-of-line completion");

    editor.loadText("A sentence.");
    type(editor, " Ins");
    QInputMethodEvent preedit("p", {});
    QApplication::sendEvent(&editor, &preedit);
    check(!ghost->isVisible() && status.isEmpty(), "input method owns its preedit preview");
    QInputMethodEvent commit;
    commit.setCommitString("p");
    QApplication::sendEvent(&editor, &commit);
    check(ghost->isVisible() && ghost->text() == "ector Goose", "completed input method text can suggest");

    SynonymWritingSettings options;
    options.charactersCompletion = SynonymCompletion::OnDemand;
    options.recurringCompletion = SynonymCompletion::Off;
    options.charactersUnderlining = SynonymUnderlining::CurrentGroup;
    options.recurringUnderlining = SynonymUnderlining::Off;
    options.underlineColor = QColor("#5478a4");
    writing.applySettings(options);
    editor.loadText("A sentence.");
    type(editor, " Hi");
    check(!ghost->isVisible() && status.isEmpty(), "on-demand mode does not show hints");
    writing.complete();
    check(list->isVisible() && list->model()->rowCount() == 1, "on-demand accepts short unique prefixes");
    key(*list, Qt::Key_Return);
    editor.loadText("A sentence.");
    type(editor, " Moo");
    writing.complete();
    check(!list->isVisible() && notice.contains("No matching"), "disabled list is excluded from completion");

    const QString text = "The lunar auditor called.\nInspector Goose saluted a goose.\nHis Feathered Excellency left.";
    editor.loadText(text);
    const int inspector = int(text.indexOf("Inspector"));
    const int last = int(text.indexOf("His"));
    editor.jumpTo(inspector + 2, false);
    settle();
    const auto ranges = editor.extraSelections();
    check(ranges.size() == 3 && ranges[1].cursor.selectedText() == "Inspector Goose" &&
          ranges[1].format.underlineStyle() == QTextCharFormat::SingleUnderline &&
          ranges[1].format.underlineColor() == options.underlineColor, "only the current group's complete names are marked");
    editor.jumpTo(inspector - 3, false);
    settle();
    check(editor.extraSelections().isEmpty(), "marking follows the caret");
    options.charactersUnderlining = SynonymUnderlining::Off;
    options.charactersCompletion = SynonymCompletion::Off;
    writing.applySettings(options);
    editor.jumpTo(last + 3, false);
    QList<int> departures;
    writing.jumpTo = [&](int position) {
        departures.append(editor.textCursor().position());
        editor.jumpTo(position);
    };
    writing.navigate(true);
    check(editor.textCursor().position() == inspector, "previous follows a different name in the same group");
    writing.navigate(true);
    check(editor.textCursor().position() == 0, "previous skips the current occurrence");
    writing.navigate(true);
    check(notice == "No earlier mention." && departures.size() == 2, "edge leaves caret and history alone");
    writing.navigate(false);
    check(editor.textCursor().position() == inspector, "forward visits in manuscript order");
    editor.jumpTo(last + 5, false);
    check(writing.follow(inspector + 1, true, true) && departures.last() == last + 5 &&
          editor.textCursor().position() == 0, "mouse lookup retains the original writing position");
    check(!writing.follow(inspector + 15, true, true) && !writing.follow(inspector + 18, true, true),
          "ordinary text and the space after a name are not pointer targets");

    matcher.setGroups({{{"Inspector Goose", "Pip"}}}, {{{"Moonrise", "Pip"}}});
    writing.listsChanged();
    editor.loadText("Pip watched Moonrise. Inspector Goose waved to Pip.");
    const int lastPip = editor.manuscriptText().lastIndexOf("Pip");
    editor.jumpTo(lastPip, false);
    const int before = int(departures.size());
    writing.navigate(true);
    check(list->isVisible() && list->model()->rowCount() == 2 && before == departures.size(), "shared name offers its groups");
    key(*list, Qt::Key_Escape);
    check(editor.textCursor().position() == lastPip && before == departures.size(), "canceling a group creates no jump");
    writing.navigate(true);
    key(*list, Qt::Key_Return);
    check(editor.textCursor().position() == editor.manuscriptText().indexOf("Inspector Goose"), "chosen group controls the jump");
    writing.navigate(true);
    writing.navigate(false);
    check(!list->isVisible() && editor.textCursor().position() == editor.manuscriptText().indexOf("Inspector Goose"),
          "repeated navigation remembers the group at ambiguous destinations");
    std::puts("Synonym writing tests passed.");
}
