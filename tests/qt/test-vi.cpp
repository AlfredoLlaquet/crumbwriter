#include "manuscripteditor.h"
#include "manuscripttext.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QScrollBar>
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
    const auto text = QStringLiteral("Test manuscript//Crumb 1: A: blue-eagle\nCrumb 2: B: A BLUE-green fox\nCrumb 3: C: red FOX\n");
    ed.loadText(text);
    QString status;
    ed.onViPrompt = [&](const QString &s) { status = s; };
    const int blue = text.indexOf("blue"), hyphen = text.indexOf("-green");
    const int end = text.indexOf("\nCrumb 3"), red = text.indexOf("red");
    auto move = [&](int from, const QString &command, int to) {
        ed.jumpTo(from, false);
        start(ed);
        type(ed, command);
        if (!ed.viSelectionActive() || ed.textCursor().anchor() != from || ed.textCursor().position() != to)
            qFatal("Motion %s from %d expected %d, got %d. Prompt: %s", qPrintable(command),
                   from, to, ed.textCursor().position(), qPrintable(status));
        key(ed, Qt::Key_Return);
        check(!ed.viSelectionActive() && ed.textCursor().anchor() == from && ed.textCursor().position() == to,
              "Enter keeps the previewed selection and returns to editing");
    };
    move(blue, "t-2", hyphen);
    check(ManuscriptText::selectedText(ed.textCursor()) == text.mid(blue, hyphen - blue),
          "t-2 selects across a physical newline and stops before the second hyphen");
    move(blue, "f-2", hyphen + 1);
    move(end, "F-2", text.indexOf('-'));
    move(end, "T-", hyphen + 1);
    move(blue, "qfOx*2", red + 7);
    move(text.size(), "QreD fOx", red);
    move(blue, "qcRuMb 2", text.indexOf("Crumb 2") + 7);
    move(text.size(), "QCrUmB 1", text.indexOf("Crumb 1"));
    move(blue, "e", blue + 4);
    move(blue, "w2", blue + 5);
    move(blue, "W", text.indexOf("Crumb 2"));
    move(blue, "E", text.indexOf('\n'));
    move(hyphen, "b", text.indexOf("BLUE"));
    move(hyphen, "B", text.indexOf("BLUE"));
    move(blue, "h", blue - 1);
    move(blue, "l2", blue + 2);
    move(blue, "$", text.indexOf('\n'));
    move(blue, "$2", end);
    move(blue, "0", 0);
    move(blue, "G", text.indexOf("Crumb 3"));
    move(blue, "G2", text.indexOf("Crumb 2"));
    move(blue, "gg2", text.indexOf("Crumb 2"));
    move(end, "gg", 0);
    move(blue, "t-", text.indexOf('-'));
    start(ed);
    type(ed, ";");
    check(ed.textCursor().position() == hyphen, "repeat till advances beyond the first excluded hyphen");
    key(ed, Qt::Key_Return);
    move(blue, "f-2", hyphen + 1);
    start(ed);
    type(ed, ",");
    check(ed.textCursor().position() == text.indexOf('-'), "reverse repeat skips the immediately preceding match");
    key(ed, Qt::Key_Return);

    ed.selectRange(blue + 2, blue, false);
    start(ed);
    type(ed, "t!");
    check(ed.viSelectionActive() && status.contains("t!") && ed.textCursor().anchor() == blue + 2 &&
          ed.textCursor().position() == blue, "failed movement preserves caret, selection and editable command");
    key(ed, Qt::Key_Backspace);
    type(ed, "-2");
    check(ed.textCursor().anchor() == blue && ed.textCursor().position() == hyphen, "repair a failed movement");
    key(ed, Qt::Key_Return);
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

void liveSelection(ManuscriptEditor &ed)
{
    const QString text = QStringLiteral("zero\nfirst\nsecond\nthird");
    ed.loadText(text);
    ed.selectRange(3, 1, false);
    QString prompt;
    int accepted = 0;
    ed.onViPrompt = [&](const QString &s) { prompt = s; };
    ed.onViAccepted = [&] { ++accepted; };
    auto caret = [&](int anchor, int position) {
        return ed.textCursor().anchor() == anchor && ed.textCursor().position() == position;
    };
    const int first = text.indexOf("first") + 1;
    const int second = text.indexOf("second") + 1;
    start(ed);
    type(ed, "j");
    check(caret(1, first) && ed.viSelectionActive(), "j previews a single physical paragraph from the original caret");
    type(ed, "2");
    check(caret(1, second) && prompt.contains("j2") && accepted == 0, "a suffix replaces the prior preview");
    key(ed, Qt::Key_Backspace);
    check(caret(1, first), "deleting a count shortens the live selection");
    type(ed, "9");
    key(ed, Qt::Key_Return);
    check(caret(3, 1) && accepted == 0 && ed.viSelectionActive(), "an unavailable target cannot be accepted");
    key(ed, Qt::Key_Backspace);
    key(ed, Qt::Key_Escape);
    check(caret(3, 1) && accepted == 0 && !ed.viSelectionActive(), "Escape restores the directed selection");
    start(ed);
    type(ed, "j2");
    key(ed, Qt::Key_Return);
    check(caret(1, second) && accepted == 1 && prompt.isEmpty(), "only Enter records a visit");
    start(ed);
    type(ed, "gg");
    key(ed, Qt::Key_Backspace);
    check(caret(1, second), "an incomplete compound command restores the original selection");
    key(ed, Qt::Key_Escape);
    start(ed);
    type(ed, "2j");
    key(ed, Qt::Key_Return);
    check(caret(1, second) && ed.viSelectionActive(), "a prefix count cannot accidentally commit");
    key(ed, Qt::Key_Escape);
    start(ed);
    type(ed, "k2147483648");
    check(caret(1, second) && prompt.contains("count", Qt::CaseInsensitive), "overflowing counts stay editable");
    key(ed, Qt::Key_Escape);

    ed.loadText("th then th*2 TH thorn th*2 \\path ***");
    const auto phrase = ed.manuscriptText();
    ed.jumpTo(0, false);
    start(ed);
    type(ed, "qt");
    check(caret(0, 1), "the first phrase character previews immediately");
    type(ed, "h");
    check(caret(0, 2), "refining a phrase selects through its last character");
    type(ed, "*");
    key(ed, Qt::Key_Return);
    check(ed.viSelectionActive() && caret(0, 0), "a bare occurrence separator waits for a count");
    type(ed, "2");
    check(caret(0, phrase.indexOf("then") + 2), "a phrase count selects the second occurrence from the origin");
    key(ed, Qt::Key_Backspace);
    key(ed, Qt::Key_Backspace);
    check(caret(0, 2), "removing the count and separator restores the first match");
    type(ed, "\\*2");
    check(caret(0, phrase.indexOf("th*2") + 4), "an escaped star belongs to the literal phrase");
    type(ed, "*2");
    check(caret(0, phrase.lastIndexOf("th*2") + 4), "a literal star and occurrence can share a phrase");
    key(ed, Qt::Key_Return);
    start(ed);
    type(ed, "Qth*2");
    check(ed.textCursor().position() == phrase.indexOf("thorn"), "backward phrases count before the original caret");
    key(ed, Qt::Key_Return);
    start(ed);
    type(ed, "q\\\\path");
    check(ed.textCursor().position() == phrase.indexOf("\\path") + 5, "an escaped backslash matches literally");
    key(ed, Qt::Key_Return);

    ed.jumpTo(0, false);
    start(ed);
    type(ed, "f22");
    check(ed.textCursor().position() == phrase.lastIndexOf("2") + 1, "a character find can count repetitions of a digit");
    key(ed, Qt::Key_Return);
    start(ed);
    type(ed, "Fh");
    key(ed, Qt::Key_Escape);
    ed.jumpTo(0, false);
    start(ed);
    type(ed, ";");
    check(ed.textCursor().position() == phrase.indexOf("2") + 1, "a canceled find leaves the last accepted target intact");
    key(ed, Qt::Key_Return);
    ed.onViPrompt = {};
    ed.onViAccepted = {};
}

void compositionAndWhitespace(ManuscriptEditor &ed)
{
    const QString text = QStringLiteral("Café and café");
    ed.loadText(text);
    ed.jumpTo(0, false);
    start(ed);
    type(ed, "qcaf");
    QInputMethodEvent composing(QStringLiteral("é"), {});
    QApplication::sendEvent(&ed, &composing);
    check(ed.textCursor().position() == 4 && ed.viSelectionActive(), "preedit participates in the live command");
    QInputMethodQueryEvent query(Qt::ImSurroundingText | Qt::ImCursorPosition | Qt::ImCurrentSelection);
    QApplication::sendEvent(&ed, &query);
    check(query.value(Qt::ImSurroundingText).toString() == "qcaf" && query.value(Qt::ImCursorPosition).toInt() == 4 &&
          query.value(Qt::ImCurrentSelection).toString().isEmpty(), "the IME sees the command and its own caret");
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("é"));
    QApplication::sendEvent(&ed, &commit);
    type(ed, "*1");
    QInputMethodEvent replace;
    replace.setCommitString("2", -1, 1);
    QApplication::sendEvent(&ed, &replace);
    check(ed.textCursor().position() == text.size() && ed.manuscriptText() == text,
          "an IME replacement edits the command and previews its new count");
    key(ed, Qt::Key_Return);

    ed.loadText(QStringLiteral("Bee 🐝 and 🐝 again"));
    ed.jumpTo(0, false);
    start(ed);
    type(ed, "f");
    QInputMethodEvent emoji;
    emoji.setCommitString(QStringLiteral("🐝"));
    QApplication::sendEvent(&ed, &emoji);
    check(ed.textCursor().selectedText() == QStringLiteral("Bee 🐝"), "a committed emoji forms one find target");
    key(ed, Qt::Key_Backspace);
    check(!ed.textCursor().hasSelection() && ed.viSelectionActive(), "Backspace removes one whole target grapheme");
    type(ed, "a");
    check(ed.textCursor().selectedText() == QStringLiteral("Bee 🐝 a"), "the replacement target starts at the opening caret");
    key(ed, Qt::Key_Escape);

    ed.loadText(QStringLiteral("alpha"));
    ed.jumpTo(5, false);
    key(ed, Qt::Key_Space, " ");
    start(ed);
    type(ed, "h2");
    check(ed.manuscriptText() == "alpha ", "preview preserves a provisional trailing space");
    key(ed, Qt::Key_Escape);
    key(ed, Qt::Key_Z, "z");
    check(ed.manuscriptText() == "alpha z", "writing after cancel resumes at the original provisional space");
}

void restoreViewport(ManuscriptEditor &ed)
{
    QStringList lines;
    for (int line = 1; line <= 150; ++line)
        lines.append(QStringLiteral("Paragraph %1.").arg(line));
    ed.resize(340, 260);
    ed.loadText(lines.join(u'\n'));
    ed.show();
    QApplication::processEvents();
    const auto origin = ed.document()->findBlockByNumber(70).position();
    ed.selectRange(origin + 4, origin);
    const auto scroll = ed.verticalScrollBar()->value();
    const auto y = ed.cursorRect().top();
    start(ed);
    type(ed, "G");
    check(ed.verticalScrollBar()->value() > scroll, "a remote preview brings its endpoint into view");
    key(ed, Qt::Key_Escape);
    check(ed.verticalScrollBar()->value() == scroll && ed.cursorRect().top() == y &&
          ed.textCursor().anchor() == origin + 4 && ed.textCursor().position() == origin,
          "cancel restores the scroll position as well as both selection endpoints");

    ed.openAtEnd();
    key(ed, Qt::Key_Down);
    key(ed, Qt::Key_Down);
    ed.centerCurrentLine();
    const auto virtualScroll = ed.verticalScrollBar()->value();
    check(ed.virtualRows() == 2, "virtual origin installed");
    start(ed);
    type(ed, "gg");
    check(!ed.virtualRows() && ed.textCursor().position() == 0, "a virtual caret can preview a real destination");
    key(ed, Qt::Key_Escape);
    check(ed.virtualRows() == 2 && ed.verticalScrollBar()->value() == virtualScroll,
          "cancel also restores virtual rows and their viewport");
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
    auto go = [&](const QString &motion) { start(ed); type(ed, motion); key(ed, Qt::Key_Return); };
    go("gj");
    check(ed.textCursor().block() == block && visualRow() == 1, "gj selects into the next screen row of the same crumb");
    const int col = ed.textCursor().positionInBlock();
    go("j");
    check(ed.textCursor().blockNumber() == 1 && ed.textCursor().positionInBlock() == col, "j selects into the next crumb");
    go("k");
    check(ed.textCursor().block() == block && ed.textCursor().positionInBlock() == col, "k returns by physical line");
    go("gk");
    check(ed.textCursor().block() == block && visualRow() == 0, "gk returns by screen row");
    go("gj2");
    check(visualRow() == 2, "counted visual rows");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ManuscriptEditor editor;
    commands(editor);
    liveSelection(editor);
    compositionAndWhitespace(editor);
    restoreViewport(editor);
    wrappedRows(editor);
}
