#include "manuscripteditor.h"
#include "manuscripttext.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextBlock>

namespace {
void check(bool ok, const char *message)
{
    if (!ok) qFatal("%s", message);
}

void key(ManuscriptEditor &editor, int code, const QString &text = {}, Qt::KeyboardModifiers mods = {})
{
    QKeyEvent event(QEvent::KeyPress, code, mods, text);
    QApplication::sendEvent(&editor, &event);
}

void type(ManuscriptEditor &editor, const QString &text)
{
    for (QChar ch : text) key(editor, ch.toUpper().unicode(), QString(ch));
}

void start(ManuscriptEditor &editor) { key(editor, Qt::Key_Space, {}, Qt::ControlModifier); }

void commands(ManuscriptEditor &ed)
{
    const auto text = QStringLiteral("Crumb 1: A: blue-eagle\nCrumb 2: B: A BLUE-green fox\nCrumb 3: C: red FOX\n");
    ed.loadText(text);
    QString status;
    ed.onViPrompt = [&](const QString &s) { status = s; };
    const int blue = text.indexOf("blue"), hyphen = text.indexOf("-green");
    const int end = text.indexOf("\nCrumb 3"), red = text.indexOf("red");
    auto move = [&](int from, const QString &command, int to, bool enter = false) {
        ed.jumpTo(from, false);
        start(ed);
        type(ed, command);
        if (enter) key(ed, Qt::Key_Return);
        if (ed.viSelectionActive() || ed.textCursor().anchor() != from || ed.textCursor().position() != to)
            qFatal("Motion %s from %d expected %d, got %d. Prompt: %s", qPrintable(command),
                   from, to, ed.textCursor().position(), qPrintable(status));
    };
    move(blue, "2t-", hyphen);
    check(ManuscriptText::selectedText(ed.textCursor()) == text.mid(blue, hyphen - blue),
          "2t- selects across a physical newline and stops before the second hyphen");
    move(blue, "2f-", hyphen + 1);
    move(end, "2F-", text.indexOf('-'));
    move(end, "T-", hyphen + 1);
    move(blue, "2qfOx", red + 7, true);
    move(text.size(), "QreD fOx", red, true);
    move(blue, "qcRuMb 2", text.indexOf("Crumb 2") + 7, true);
    move(text.size(), "QCrUmB 1", 0, true);
    move(blue, "e", blue + 4);
    move(blue, "2w", blue + 5);
    move(blue, "W", text.indexOf("Crumb 2"));
    move(blue, "E", text.indexOf('\n'));
    move(hyphen, "b", text.indexOf("BLUE"));
    move(hyphen, "B", text.indexOf("BLUE"));
    move(blue, "h", blue - 1);
    move(blue, "2l", blue + 2);
    move(blue, "$", text.indexOf('\n'));
    move(blue, "2$", end);
    move(blue, "0", 0);
    move(blue, "G", text.indexOf("Crumb 3"));
    move(blue, "2gg", text.indexOf("Crumb 2"));
    move(end, "gg", 0);
    move(blue, "t-", text.indexOf('-'));
    start(ed);
    type(ed, ";");
    check(ed.textCursor().position() == hyphen, "repeat till advances beyond the first excluded hyphen");
    move(blue, "2f-", hyphen + 1);
    start(ed);
    type(ed, ",");
    check(ed.textCursor().position() == text.indexOf('-'), "reverse repeat skips the immediately preceding match");

    ed.selectRange(blue + 2, blue, false);
    start(ed);
    type(ed, "2t!");
    check(ed.viSelectionActive() && status.contains("2t!") && ed.textCursor().anchor() == blue + 2 &&
          ed.textCursor().position() == blue, "failed movement preserves caret, selection and editable command");
    key(ed, Qt::Key_Backspace);
    type(ed, "-");
    check(ed.textCursor().anchor() == blue && ed.textCursor().position() == hyphen, "repair a failed movement");
    start(ed);
    type(ed, "qmissing");
    key(ed, Qt::Key_Return);
    key(ed, Qt::Key_Escape);
    check(!ed.viSelectionActive() && status.isEmpty() && ed.textCursor().anchor() == blue &&
          ed.textCursor().position() == hyphen, "Escape from a failed phrase preserves the old selection");
    ed.jumpTo(0, false);
    start(ed);
    type(ed, "q");
    QApplication::clipboard()->setText("eagle\nCrumb 2");
    key(ed, Qt::Key_V, {}, Qt::ControlModifier);
    key(ed, Qt::Key_Return);
    check(ed.textCursor().position() == text.indexOf("Crumb 2") + 7, "literal query across a newline");
    check(ed.manuscriptText() == text && !ed.document()->isUndoAvailable(), "vi commands leave file bytes and undo intact");

    ed.loadText(QStringLiteral("Crumb 1: : A👩🏽‍🔬 Bébé"));
    ed.jumpTo(0, false);
    start(ed);
    type(ed, "f");
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("👩🏽‍🔬"));
    QApplication::sendEvent(&ed, &commit);
    check(ed.textCursor().selectedText().endsWith(QStringLiteral("👩🏽‍🔬")), "one typed grapheme can be a character target");
    ed.loadText(QStringLiteral("Crumb 1: : a\u0301"));
    move(0, "fa", ed.manuscriptText().size());
    ed.onViPrompt = {};
}

void wrappedRows(ManuscriptEditor &ed)
{
    const auto row = QStringLiteral("Crumb 1: Wrap: ") + QStringLiteral("many words ").repeated(18);
    ed.resize(260, 340);
    ed.loadText(row + QStringLiteral("\nCrumb 2: Wrap: ") + QStringLiteral("more words ").repeated(16));
    ed.show();
    QApplication::processEvents();
    ed.jumpTo(3, false);
    const auto block = ed.textCursor().block();
    auto visualRow = [&] {
        auto cursor = ed.textCursor();
        return cursor.block().layout()->lineForTextPosition(cursor.positionInBlock()).lineNumber();
    };
    auto go = [&](const QString &motion) { start(ed); type(ed, motion); };
    go("gj");
    check(ed.textCursor().block() == block && visualRow() == 1, "gj selects into the next screen row of the same crumb");
    const int col = ed.textCursor().positionInBlock();
    go("j");
    check(ed.textCursor().blockNumber() == 1 && ed.textCursor().positionInBlock() == col, "j selects into the next crumb");
    go("k");
    check(ed.textCursor().block() == block && ed.textCursor().positionInBlock() == col, "k returns by physical line");
    go("gk");
    check(ed.textCursor().block() == block && visualRow() == 0, "gk returns by screen row");
    go("2gj");
    check(visualRow() == 2, "counted visual rows");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ManuscriptEditor editor;
    commands(editor);
    wrappedRows(editor);
}
