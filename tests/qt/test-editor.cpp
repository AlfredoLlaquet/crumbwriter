#include "manuscripteditor.h"
#include "storage.h"
extern "C" {
#include "crumb.h"
}

#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QTextBlock>
#include <QtWidgets/QApplication>
#include <QtWidgets/QVBoxLayout>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool ok, const char *message)
{
    if (!ok) {
        std::fprintf(stderr, "Editor check failed: %s\n", message);
        std::exit(1);
    }
}

void expect(ManuscriptEditor &ed, const QString &wanted)
{
    if (ed.manuscriptText() != wanted) {
        std::fprintf(stderr, "Expected <%s>, got <%s>\n", qPrintable(wanted), qPrintable(ed.manuscriptText()));
        std::exit(1);
    }
}

void press(ManuscriptEditor &ed, int key, Qt::KeyboardModifiers mods = {}, const QString &text = {})
{
    QKeyEvent event(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(&ed, &event);
}

void ime(ManuscriptEditor &ed, const QString &text)
{
    QInputMethodEvent e;
    e.setCommitString(text);
    QApplication::sendEvent(&ed, &e);
}

void type(ManuscriptEditor &ed, const QString &text)
{
    for (QChar ch : text)
        press(ed, ch.toUpper().unicode(), {}, QString(ch));
}

void settle() { for (int i = 0; i < 4; ++i) QApplication::processEvents(); }

void punctuation(ManuscriptEditor &ed)
{
    ed.loadText({});
    type(ed, QStringLiteral("I'"));
    expect(ed, QStringLiteral("I'"));
    type(ed, QStringLiteral("d Pepe,\""));
    expect(ed, QStringLiteral("I’d Pepe,\""));
    type(ed, QStringLiteral(" new--"));
    expect(ed, QStringLiteral("I’d Pepe,” new--"));
    ime(ed, QStringLiteral("sí... "));
    expect(ed, QStringLiteral("I’d Pepe,” new—sí… "));
    ed.undo();
    expect(ed, {});
    ed.redo();
    expect(ed, QStringLiteral("I’d Pepe,” new—sí… "));

    ed.loadText({});
    type(ed, QStringLiteral("\"The dogs' collars are 'blue,'\" said Alex. 'Tis the '90s. 'Tissue' "));
    expect(ed, QStringLiteral("“The dogs’ collars are ‘blue,’” said Alex. ’Tis the ’90s. ‘Tissue’ "));
    ed.loadText(QStringLiteral("Old.."));
    type(ed, QStringLiteral(". "));
    expect(ed, QStringLiteral("Old... ")); // The two original file periods remain literal.
    ed.loadText(QStringLiteral("Plain text"));
    ed.selectRange(6, 10, false);
    ime(ed, QStringLiteral("\"é😀--b...\" "));
    expect(ed, QStringLiteral("Plain “é😀—b…” "));
    ed.undo();
    expect(ed, QStringLiteral("Plain text"));
    ed.loadText({});
    type(ed, QStringLiteral("word--"));
    ed.jumpTo(0, false);
    expect(ed, QStringLiteral("word—"));
    ed.loadText(QStringLiteral("word XXX"));
    ed.jumpTo(5, false);
    ed.setOverwriteMode(true);
    ime(ed, QStringLiteral("-"));
    ime(ed, QStringLiteral("-"));
    ime(ed, QStringLiteral("s"));
    expect(ed, QStringLiteral("word —s"));
    ed.undo();
    expect(ed, QStringLiteral("word XXX"));
    ed.setOverwriteMode(false);
    ed.loadText(QStringLiteral("word—"));
    ed.jumpTo(0, false);
    ed.setSmartPunctuation(false);
    type(ed, QStringLiteral("'--... "));
    expect(ed, QStringLiteral("'--... word—"));
    ed.setSmartPunctuation(true);
}

void spacing(ManuscriptEditor &ed)
{
    ed.loadText(QStringLiteral("Leaf 1: : one\nLeaf 2: : two"));
    ed.jumpTo(QStringLiteral("Leaf 1: : one").size(), false);
    type(ed, QStringLiteral("  "));
    expect(ed, QStringLiteral("Leaf 1: : one \nLeaf 2: : two"));
    type(ed, QStringLiteral("b "));
    press(ed, Qt::Key_Down, Qt::ControlModifier);
    expect(ed, QStringLiteral("Leaf 1: : one b\nLeaf 2: : two"));
    ed.loadText(QStringLiteral("Leaf 1: : é\tX z."));
    ed.jumpTo(QStringLiteral("Leaf 1: : é ").size(), false);
    press(ed, Qt::Key_Delete);
    expect(ed, QStringLiteral("Leaf 1: : é z."));
    ed.undo();
    expect(ed, QStringLiteral("Leaf 1: : é X z."));
    require(!ed.document()->isUndoAvailable(), "a deletion and its spacing cleanup take one undo");

    ed.loadText(QStringLiteral("Leaf 1: : a"));
    ime(ed, QStringLiteral("b "));
    press(ed, Qt::Key_Backspace);
    expect(ed, QStringLiteral("Leaf 1: : ab"));
    ed.undo();
    type(ed, QStringLiteral("c"));
    expect(ed, QStringLiteral("Leaf 1: : ab c"));

    ed.loadText(QStringLiteral("Leaf 1: : A\u00a0B"));
    expect(ed, QStringLiteral("Leaf 1: : A\u00a0B"));
    QApplication::clipboard()->setText(QStringLiteral("\t\t\"old--words...\"  \r\nLeaf\t2: :  and   now\t "));
    ed.paste();
    expect(ed, QStringLiteral("Leaf 1: : A\u00a0B \"old--words...\"\nLeaf 2: : and now"));
    ed.undo();
    expect(ed, QStringLiteral("Leaf 1: : A\u00a0B"));
    ed.loadText(QStringLiteral("Leaf 1: : x between y"));
    const int pos = QStringLiteral("Leaf 1: : x ").size();
    ed.replaceRangeLiteral(pos, pos+7, QStringLiteral("  "));
    expect(ed, QStringLiteral("Leaf 1: : x y"));
}

void rewritingLastWord(ManuscriptEditor &ed)
{
    const QString head = QStringLiteral("Leaf 1: : red");
    const QString next = QStringLiteral("\nLeaf 2: : end\n");
    const auto original = head + QStringLiteral(" fox") + next;
    QTemporaryDir temp;
    const auto path = temp.filePath(QStringLiteral("rewrite.txt"));
    QFile disk(path);
    require(disk.open(QIODevice::WriteOnly) && disk.write(original.toUtf8()) == original.size(),
            "create last-word fixture");
    disk.close();
    gchar *loaded = nullptr;
    GError *error = nullptr;
    CwFile *file = cw_file_open(QFile::encodeName(path).constData(), false, &loaded, &error);
    require(file && !error, "open last-word fixture");
    ed.loadText(QString::fromUtf8(loaded));
    g_free(loaded);
    ed.jumpTo(original.indexOf(u'\n'), false);
    for (int i = 0; i < 3; ++i)
        press(ed, Qt::Key_Backspace);
    expect(ed, head + u' ' + next);
    require(ed.textCursor().atBlockEnd() && ed.textCursor().position() == head.size() + 1,
            "backspacing the last word retains its preceding space at the caret");
    require(cw_file_save(file, ed.manuscriptText().toUtf8().constData(), &error) && !error,
            "save while a preceding separator is awaiting the replacement word");
    require(disk.open(QIODevice::ReadOnly) && disk.readAll() == (head + next).toUtf8(),
            "saved bytes drop the provisional space while the editing caret keeps it");
    disk.close();
    type(ed, QStringLiteral("panda"));
    expect(ed, head + QStringLiteral(" panda") + next);
    for (int i = 0; i < 5; ++i)
        press(ed, Qt::Key_Backspace);
    press(ed, Qt::Key_Down, Qt::ControlModifier);
    expect(ed, head + next);
    cw_file_free(file);
}

void wholeWordDeletion(ManuscriptEditor &ed)
{
    const auto text = QStringLiteral("Note 1: : A she’d half-eaten 👩🏽‍🔬.\nNote 2: : last\n");
    auto erase = [&](int pos, const QString &expected) {
        ed.loadText(text);
        ed.jumpTo(pos, false);
        press(ed, Qt::Key_W, Qt::ControlModifier);
        expect(ed, expected);
        ed.undo();
        expect(ed, text);
        require(!ed.document()->isUndoAvailable(), "whole-word deletion is one undoable edit");
    };
    erase(text.indexOf(u'’'), QString(text).remove(QStringLiteral("she’d ")));
    erase(text.indexOf(u'-'), QString(text).remove(QStringLiteral("half-eaten ")));
    const int space = text.indexOf(QStringLiteral(" she’d"));
    erase(space, QString(text).remove(space, 1));
    erase(text.indexOf(QStringLiteral("👩")), QString(text).remove(QStringLiteral("👩🏽‍🔬")));
    const int lastStart = text.indexOf(QStringLiteral("last"));
    erase(lastStart+4, QString(text).remove(QStringLiteral("last")));
    ed.selectRange(lastStart+1, lastStart+3, false);
    press(ed, Qt::Key_W, Qt::ControlModifier);
    expect(ed, QString(text).replace(QStringLiteral("last"), QStringLiteral("lt")));
    ed.undo();
    expect(ed, text);
    ed.jumpTo(text.size(), false); // Empty physical line after the last LF.
    press(ed, Qt::Key_W, Qt::ControlModifier);
    expect(ed, text);
    require(!ed.document()->isUndoAvailable(), "Ctrl+W preserves paragraph boundaries");

    QApplication::clipboard()->setText(QStringLiteral("clipboard"));
    ed.selectRange(lastStart, lastStart+4, false);
    ed.insertLiteral(QStringLiteral("new--name"));
    expect(ed, QString(text).replace(QStringLiteral("last"), QStringLiteral("new--name")));
    require(QApplication::clipboard()->text() == QStringLiteral("last") && !ed.textCursor().hasSelection(),
            "literal insertion copies the displaced selection");
    ed.undo();
    expect(ed, text);
}

void deletionClipboard(ManuscriptEditor &ed)
{
    auto *clipboard = QApplication::clipboard();
    ed.loadText("A goose");
    for (int i = 0; i < 5; ++i) press(ed, Qt::Key_Backspace);
    require(clipboard->text() == "goose", "backward deletions accumulate in reading order");
    ed.undo();
    require(clipboard->text() == "goose", "undo preserves the last deliberate deletion");
    ed.jumpTo(2, false);
    press(ed, Qt::Key_Delete);
    press(ed, Qt::Key_Delete);
    require(clipboard->text() == "go", "forward deletions build a fresh clipboard entry");
    press(ed, Qt::Key_Backspace);
    require(clipboard->text() == " ", "reversing deletion direction starts a new entry");

    ed.loadText("First red fox.\nAnother fox.");
    ed.jumpTo(7, false);
    press(ed, Qt::Key_W, Qt::ControlModifier);
    require(clipboard->text() == "red", "word deletion copies the word without automatic space cleanup");
    ed.undo();
    ed.selectRange(6,9,false);
    clipboard->setText("blue");
    ed.paste();
    expect(ed, "First blue fox.\nAnother fox.");
    require(clipboard->text() == "red", "paste reads its input before publishing the displaced selection");
    ed.selectRange(10,6,false);
    type(ed,"gold");
    require(clipboard->text() == "blue", "typing over a backward selection places it on the clipboard");
    ed.jumpTo(0,false);
    press(ed, Qt::Key_Y, Qt::ControlModifier);
    require(clipboard->text() == "First gold fox.\n", "whole paragraph deletion includes its actual line break");
    ed.undo();
    ed.redo();
    require(clipboard->text() == "First gold fox.\n", "replaying deletion does not change the clipboard");
    ed.replaceRangeLiteral(0,7,"Second");
    require(clipboard->text() == "Another", "a deliberate range replacement copies the source passage");

    ed.loadText("The bill.");
    ed.selectRange(4,8,false);
    ime(ed, "beak");
    require(clipboard->text() == "bill", "input method commits capture replaced selections");
    clipboard->setText("keep");
    ed.selectRange(4,8,false);
    ed.setCopyDeletedText(false);
    type(ed, "fee");
    press(ed, Qt::Key_Backspace);
    require(clipboard->text() == "keep", "disabling automatic copy preserves the clipboard");
    ed.selectRange(0,3,false);
    ed.cut();
    require(clipboard->text() == "The", "explicit Cut still copies when automatic copy is off");
    ed.setCopyDeletedText(true);
    ed.loadText("A");
    clipboard->setText("keep");
    type(ed, " new--s ");
    ed.jumpTo(0,false);
    require(clipboard->text() == "keep", "smart punctuation and automatic trimming preserve the clipboard");
}

void appendUndoPosition(ManuscriptEditor &ed)
{
    ed.resize(360,265);
    ed.show();
    ed.setFocus();
    for (int kind = 0; kind < 3; ++kind) {
        const bool generic = kind == 2;
        QString manuscript = generic ? QString() : QStringLiteral("Book//");
        for (int i = 1; i <= 55; ++i) {
            if (!generic) manuscript += QStringLiteral("Note %1: Haiku #%1: ").arg(i);
            manuscript += QStringLiteral("The inspector audited a goose. ").repeated(5).trimmed() + u'\n';
        }
        ed.loadText(manuscript);
        const int caret = ed.document()->findBlockByNumber(27).position() + 70;
        const int anchor = caret + 9;
        ed.selectRange(anchor,caret);
        settle();
        const auto cursorY = [&] { return ed.viewport()->mapTo(&ed, ed.cursorRect().center()).y(); };
        const int initialY = cursorY();
        const int top = ed.cursorForPosition(QPoint(4, 2)).position();
        if (kind == 1) ed.appendCrumb("Haiku");
        else press(ed, Qt::Key_N, Qt::ControlModifier);
        const int body = ed.textCursor().position();
        press(ed, Qt::Key_Z, Qt::ControlModifier);
        settle();
        expect(ed, manuscript);
        require(ed.textCursor().position() == caret && ed.textCursor().anchor() == anchor,
                "undo append restores the precise selection and its direction");
        require(qAbs(initialY - cursorY()) <= 1 && ed.cursorForPosition(QPoint(4, 2)).position() == top,
                "undo append restores the original viewport into a wrapped manuscript");
        press(ed, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        require(ed.textCursor().position() == body && ed.textCursor().atBlockEnd() && !ed.textCursor().hasSelection(),
                "redo append resumes at the new body or paragraph");
        ed.undo();
        type(ed,"a");
        require(!ed.document()->isRedoAvailable(), "an edit after undo discards the old append position");
    }
    ed.loadText("Book//Leaf 1: : A");
    press(ed, Qt::Key_Down);
    press(ed, Qt::Key_Down);
    ed.topCurrentCrumb();
    press(ed, Qt::Key_N, Qt::ControlModifier);
    ed.undo();
    require(ed.virtualRows() == 2 && !ed.document()->isModified(),
            "undo append restores a virtual writing row without adding manuscript data");
    ed.redo();
    press(ed, Qt::Key_Up, Qt::ControlModifier);
    ed.undo();
    ed.redo();
    require(ed.textCursor().blockNumber() == 1 && ed.textCursor().atBlockEnd(),
            "redo resumes at the appended body after its provisional space was trimmed");
}

void undoAndBoundaries(ManuscriptEditor &ed)
{
    const QString one = QStringLiteral("Leaves//Leaf 1: : A");
    const QString two = one + QStringLiteral("\nLeaf 2: :");
    const QString three = two + QStringLiteral("\nLeaf 3: :");
    ed.loadText(one);
    ed.appendCrumb();
    press(ed, Qt::Key_Up, Qt::ControlModifier);
    expect(ed, two);
    ed.appendCrumb();
    press(ed, Qt::Key_Up, Qt::ControlModifier);
    expect(ed, three);
    ed.undo();
    expect(ed, two);
    ed.undo();
    expect(ed, one);
    require(!ed.document()->isUndoAvailable(), "departing a provisional body adds no isolated undo");
    ed.redo();
    press(ed, Qt::Key_PageUp, Qt::ControlModifier);
    ed.redo();
    press(ed, Qt::Key_PageUp, Qt::ControlModifier);
    expect(ed, three);

    ed.loadText(two);
    ed.jumpTo(one.size(), false);
    type(ed, QStringLiteral(" "));
    ed.finishTyping(); // Explicit save settles punctuation; writer cleans its snapshot.
    const auto saved = ManuscriptEditor::normalizeInsertion(ed.manuscriptText());
    require(saved == two, "live snapshot strips only the pending separator");
    expect(ed, one + QStringLiteral(" \nLeaf 2: :"));
    ed.appendCrumb();
    ed.undo();
    expect(ed, one + QStringLiteral(" \nLeaf 2: :"));
    require(ed.textCursor().position() == one.size()+1, "undo returns to prior provisional separator");
    type(ed, QStringLiteral("b"));
    expect(ed, one + QStringLiteral(" b\nLeaf 2: :"));

    const QString original = QStringLiteral("Leaf 1: : X red Y");
    ed.loadText(original);
    const int red = original.indexOf(QStringLiteral("red"));
    ed.selectRange(red, red+3, false);
    QApplication::clipboard()->setText(QStringLiteral("\tcode   blue\t"));
    press(ed, Qt::Key_V, Qt::ControlModifier);
    expect(ed, QStringLiteral("Leaf 1: : X code blue Y"));
    press(ed, Qt::Key_Z, Qt::ControlModifier);
    expect(ed, original);
    ed.selectRange(red, red+3, false);
    press(ed, Qt::Key_X, Qt::ControlModifier);
    expect(ed, QStringLiteral("Leaf 1: : X Y"));
    require(QApplication::clipboard()->text() == QStringLiteral("red"), "keyboard cut writes native clipboard");
    press(ed, Qt::Key_Z, Qt::ControlModifier);
    expect(ed, original);

    ed.loadText(QStringLiteral("Leaf 1: : broken--"));
    ed.openAtEnd();
    type(ed, QStringLiteral("s"));
    expect(ed, QStringLiteral("Leaf 1: : broken--s")); // Existing file punctuation stays literal.
    type(ed, QStringLiteral("--"));
    ed.finishTyping();
    expect(ed, QStringLiteral("Leaf 1: : broken--s—"));
}

void editRuns(ManuscriptEditor &ed)
{
    const QString original = QStringLiteral("Crumb 1: : ");
    ed.loadText(original + u'x');
    auto undo = [&] { press(ed, Qt::Key_Z, Qt::ControlModifier); };
    auto redo = [&] { press(ed, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); };
    type(ed, " red");
    type(ed, " fox");
    press(ed, Qt::Key_Backspace);
    press(ed, Qt::Key_Backspace);
    expect(ed, original + "x red f");
    undo();
    expect(ed, original + "x red fox");
    undo();
    expect(ed, original + u'x');
    require(!ed.document()->isUndoAvailable(), "one typing run spans multiple words");
    redo();
    redo();
    expect(ed, original + "x red f");

    ed.jumpTo(original.size(), false);
    press(ed, Qt::Key_Delete);
    press(ed, Qt::Key_Delete);
    press(ed, Qt::Key_Backspace);
    undo();
    expect(ed, original + "ed f");
    undo();
    expect(ed, original + "x red f");

    ed.loadText("one");
    type(ed, " two");
    press(ed, Qt::Key_Left);
    press(ed, Qt::Key_Right);
    type(ed, " three");
    press(ed, Qt::Key_Return);
    type(ed, "next");
    ed.insertLiteral(" pasted");
    type(ed, " after");
    for (const auto expected : {"one two three\nnext pasted", "one two three\nnext",
                                "one two three\n", "one two three", "one two", "one"}) {
        undo();
        expect(ed, expected);
    }

    ed.loadText("start");
    type(ed, " new--");
    press(ed, Qt::Key_Left);
    expect(ed, QStringLiteral("start new—"));
    undo();
    expect(ed, "start");
    redo();
    expect(ed, QStringLiteral("start new—"));

    ed.loadText("red fox");
    ed.insertLiteral("!");
    ed.jumpTo(4, false);
    type(ed, " x");
    undo();
    expect(ed, "red fox!");
    undo();
    expect(ed, "red fox");
}

void shiftNavigation(ManuscriptEditor &ed)
{
    const auto text = QStringLiteral("Book//Note 1: A: first\nNote 2: : middle\nNote 3: Z: last\n\n");
    ed.loadText(text);
    const int anchor = text.indexOf("middle") + 3;
    ed.jumpTo(anchor, false);
    auto select = [&](int key, int dest) {
        press(ed, key, Qt::ControlModifier | Qt::ShiftModifier);
        require(ed.textCursor().anchor() == anchor && ed.textCursor().position() == dest,
                "modified crumb/file navigation always retains its starting anchor");
    };
    select(Qt::Key_End, text.indexOf("\nNote 3"));
    press(ed, Qt::Key_Left, Qt::ShiftModifier);
    require(ed.textCursor().anchor() == anchor, "ordinary Shift motion extends the same selection");
    select(Qt::Key_Home, text.indexOf("Note 2"));
    select(Qt::Key_Up, text.indexOf("Note 1"));
    select(Qt::Key_Down, text.indexOf("Note 2"));
    select(Qt::Key_Down, text.indexOf("Note 3"));
    select(Qt::Key_PageDown, text.indexOf("\n\n"));
    select(Qt::Key_PageUp, 0);
    select(Qt::Key_End, text.indexOf("\nNote 2"));
    select(Qt::Key_Home, text.indexOf("Note 1"));
    expect(ed, text);
}

void unicodeRoundTrip(ManuscriptEditor &ed)
{
    // Qt uses a paragraph separator and two otherwise valid noncharacters as
    // internal control markers. In the file only CR or LF begins another crumb.
    const auto prefix = QStringLiteral("Pages//Crumb 1: A");
    const auto title = QStringLiteral("A") + QChar(0x2029) + u'B';
    const auto first = prefix + QChar(0x2029) + QStringLiteral("B: x")
        + QChar(0x00a0) + u'y' + QChar(0x2028) + u'z' + QChar(0xfdd0)
        + QStringLiteral("literal") + QChar(0xfdd1) + QChar(0xfffd);
    const auto original = first + QStringLiteral("\nCrumb 2: End: ok");
    ed.loadText(original);
    expect(ed, original);
    require(ed.blockCount()==2, "literal Unicode separators retain physical line numbers");
    require(ed.model().record(0).title == title, "sidebar title keeps literal Unicode");
    require(ed.model().record(0).words == int(cw_crumb_words(first.toUtf8().constData(), true)),
            "official word count sees original Unicode code points");
    ed.jumpTo(0,false);
    ed.copyCrumb();
    require(QApplication::clipboard()->text() == first.sliced(first.indexOf("//") + 2),
            "clipboard restores literal Unicode and excludes the manuscript title");
    ed.jumpTo(prefix.size()+1,false);
    type(ed, QStringLiteral("Z"));
    expect(ed, QString(original).insert(prefix.size()+1, u'Z'));
    ed.undo();
    expect(ed, original);
    ed.jumpTo(prefix.size()+1,false);
    press(ed, Qt::Key_Backspace);
    expect(ed, QString(original).remove(prefix.size(), 1));
    ed.undo();
    expect(ed, original);
    ed.openAtEnd();
    QApplication::clipboard()->setText(QString(QChar(0x2029)) + QChar(0xfdd0));
    ed.paste();
    expect(ed, original + QChar(0x2029) + QChar(0xfdd0));
    ed.undo();
    expect(ed, original);

    QTemporaryDir isolated;
    require(isolated.isValid(), "disposable storage directory");
    const auto path = isolated.filePath(QStringLiteral("Manuscript.txt"));
    const QByteArray diskBytes = first.toUtf8() + "\r\nCrumb 2: End: ok";
    QFile disk(path);
    require(disk.open(QIODevice::WriteOnly) &&
            disk.write(QByteArray::fromHex("efbbbf") + diskBytes) == diskBytes.size()+3, "write UTF-8 fixture");
    disk.close();
    gchar *decoded = nullptr;
    GError *error = nullptr;
    auto *file = cw_file_open(QFile::encodeName(path).constData(), true, &decoded, &error);
    require(file && !error, "production storage opens noncharacters and Unicode separators");
    ed.loadText(QString::fromUtf8(decoded));
    g_free(decoded);
    type(ed, QStringLiteral("!"));
    require(cw_file_save(file, ed.manuscriptText().toUtf8().constData(), &error) && !error,
            "production storage saves the edited Unicode manuscript");
    QFile backup(QFile::decodeName(cw_file_backup_path(file)));
    require(backup.open(QIODevice::ReadOnly) && backup.readAll() == QByteArray::fromHex("efbbbf")+diskBytes,
            "startup backup preserves original BOM and all Unicode bytes");
    require(disk.open(QIODevice::ReadOnly) && disk.readAll() == diskBytes+'!',
            "physical lines, exact Unicode bytes and CRLF survive open/Qt edit/save");
    cw_file_free(file);
}

void fieldsAndClipboard(ManuscriptEditor &ed)
{
    const auto original = QStringLiteral("Títol// Bóci 1: Haiku #1: hóla.\nBóci 2: : 👩🏽‍🔬\n");
    ed.loadText(original);
    require(ed.textCursor().blockNumber() == 1 && ed.textCursor().atBlockEnd(), "opens before final LF");
    press(ed, Qt::Key_PageUp, Qt::ControlModifier);
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == original.indexOf(QStringLiteral("Haiku")), "Tab skips work title and header");
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == original.indexOf(QStringLiteral("hóla")), "Tab reaches body");
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    press(ed, Qt::Key_1, Qt::ControlModifier);
    require(ed.textCursor().selectedText() == QStringLiteral("Bóci 1") &&
            QApplication::clipboard()->text() == QStringLiteral("bóci 1"), "Ctrl+1 copies lowercase custom label and keeps the original header selected");
    press(ed, Qt::Key_Escape);
    require(!ed.textCursor().hasSelection(), "Escape clears selection");
    press(ed, Qt::Key_Q, Qt::ControlModifier);
    require(ed.textCursor().selectedText() == QStringLiteral(" Bóci 1: Haiku #1: hóla."), "copy current crumb excludes work title");
    press(ed, Qt::Key_Down, Qt::ControlModifier);
    press(ed, Qt::Key_End, Qt::ControlModifier);
    require(ed.textCursor().blockNumber()==1 && ed.textCursor().atBlockEnd(), "Ctrl+End physical end of current body");
    press(ed, Qt::Key_Y, Qt::ControlModifier);
    expect(ed, QStringLiteral("Títol// Bóci 1: Haiku #1: hóla.\n"));
    ed.undo();
    expect(ed, original);
    ed.jumpTo(original.indexOf(QStringLiteral("hóla")), false);
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().blockNumber() == 1 && ed.textCursor().positionInBlock() == 8,
            "next crumb's empty title is a stop before the second colon");
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == original.indexOf(QStringLiteral("👩")),
            "empty title advances directly to body");
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    require(ed.textCursor().position() == original.indexOf(QStringLiteral("hóla")),
            "reverse traversal between crumbs skips both headers");
    auto trailing = original + QStringLiteral("\n\n");
    ed.loadText(trailing);
    ed.jumpTo(0, false);
    press(ed, Qt::Key_PageDown, Qt::ControlModifier);
    require(ed.textCursor().blockNumber() == 1 && ed.textCursor().atBlockEnd(),
            "Ctrl+Page Down ends the final crumb before all terminal newlines");

    ed.loadText(QStringLiteral("Title // Note 1: A: x\n"));
    ed.deleteCrumb();
    expect(ed, QStringLiteral("Title //\n"));
    ed.appendCrumb();
    expect(ed, QStringLiteral("Title //Note 1: : \n"));
    const QString haiku = QStringLiteral("Leaves//Note 1: Haiku #4: x\nNote 2: Haiku : y\n");
    ed.loadText(haiku);
    ed.jumpTo(ed.manuscriptText().lastIndexOf(QStringLiteral(": y")), false);
    type(ed, QStringLiteral("#"));
    expect(ed, QString(haiku).replace("Haiku : y", "Haiku #5: y"));
    ed.undo();
    expect(ed, haiku);
    ed.appendCrumb(QStringLiteral("Haiku"));
    expect(ed, haiku + QStringLiteral("Note 3: Haiku #5: \n"));
    press(ed, Qt::Key_Up, Qt::ControlModifier);
    ed.undo();
    expect(ed, haiku);
}

void genericParagraphs(ManuscriptEditor &ed)
{
    const QString novel = QStringLiteral("The Clock Merchant\n\nWinter\n\nPeople walked in.\n"
                                         "Next came the mayor.\n\nSpring\n\nRain.\n");
    ed.loadText(novel);
    ed.resize(720, 540);
    ed.show();
    ed.jumpTo(0, false);
    settle();
    auto block = [&](int line) { return ed.document()->findBlockByNumber(line); };
    auto rect = [&](int line) { return ed.cursorRect(QTextCursor(block(line))); };
    auto bold = [&](int line) {
        const auto ranges = block(line).layout()->formats();
        return !ranges.isEmpty() && ranges.front().format.fontWeight() == QFont::Bold;
    };
    const int body = rect(4).height();
    require(rect(0).height() > rect(2).height() && rect(2).height() > body &&
            bold(0) && bold(2) && !bold(4), "document and chapter titles have distinct bold sizes");
    require(rect(1).height() < body / 2 && rect(2).top() - rect(1).top() < body / 2 &&
            ed.cursorForPosition(rect(1).center()).blockNumber() == 1,
            "a blank paragraph has compact native layout, caret and hit testing");
    require(!ed.document()->isModified() && !ed.document()->isUndoAvailable(),
            "visual paragraph styles do not modify manuscript or undo history");
    const auto font = ed.font();
    QFont enlarged = font;
    enlarged.setPointSizeF(font.pointSizeF() * 1.5);
    ed.setFont(enlarged);
    settle();
    require(rect(4).height() > body && rect(0).height() > rect(2).height() &&
            rect(2).height() > rect(4).height() && rect(1).height() < rect(4).height() / 2,
            "relative heading and separator sizes follow the writing font");
    ed.setFont(font);

    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == block(2).position(), "Tab visits the next heading");
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == block(7).position(), "Tab skips ordinary paragraphs");
    ed.jumpTo(block(7).position() + 2, false);
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    require(ed.textCursor().position() == block(7).position(), "reverse Tab reaches this heading's start");
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    require(ed.textCursor().position() == block(2).position(), "reverse Tab visits the preceding heading");
    press(ed, Qt::Key_Down, Qt::ControlModifier);
    require(ed.textCursor().position() == block(3).position(), "Ctrl+Down visits a blank paragraph");
    QTextCharFormat underline;
    underline.setFontUnderline(true);
    QInputMethodEvent composing(QStringLiteral("café"), {{QInputMethodEvent::TextFormat, 1, 2, underline}});
    QApplication::sendEvent(&ed, &composing);
    bool underlined = false;
    for (const auto &range : block(3).layout()->formats())
        if (range.start == 1 && range.length == 2 && range.format.fontUnderline())
            underlined = true;
    require(rect(3).height() >= body - 1 && underlined &&
            ed.inputMethodQuery(Qt::ImFont).value<QFont>().pointSizeF() == font.pointSizeF(),
            "uncommitted input on compact rows keeps the writing font and candidate styling");
    QInputMethodEvent canceled;
    QApplication::sendEvent(&ed, &canceled);
    require(rect(3).height() < body / 2 && !ed.document()->isUndoAvailable(),
            "canceling composition restores the compact separator without an edit");
    expect(ed, novel);
    type(ed, "No separator.");
    settle();
    require(!bold(2) && rect(2).height() == rect(4).height(), "typing after a chapter demotes its heading");
    ed.undo();
    settle();
    expect(ed, novel);
    require(bold(2), "undo restores the adjacent chapter heading");

    ed.jumpTo(block(4).position() + 3, false);
    const int anchor = ed.textCursor().position();
    auto extend = [&](int key, int dest) {
        press(ed, key, Qt::ControlModifier | Qt::ShiftModifier);
        require(ed.textCursor().anchor() == anchor && ed.textCursor().position() == dest,
                "generic navigation extends from the original anchor");
    };
    extend(Qt::Key_End, block(5).position() - 1);
    extend(Qt::Key_Home, block(4).position());
    extend(Qt::Key_Down, block(5).position());
    extend(Qt::Key_PageDown, novel.size() - 1);
    extend(Qt::Key_PageUp, 0);

    ed.jumpTo(block(4).position() + 1, false);
    press(ed, Qt::Key_Q, Qt::ControlModifier);
    require(ed.textCursor().selectedText() == "People walked in." &&
            QApplication::clipboard()->text() == "People walked in.", "Ctrl+Q copies the whole paragraph");
    press(ed, Qt::Key_1, Qt::ControlModifier);
    require(ed.textCursor().selectedText() == "People walked in.", "Ctrl+1 leaves generic selections alone");
    press(ed, Qt::Key_Y, Qt::ControlModifier);
    expect(ed, QString(novel).remove("People walked in.\n"));
    require(ed.textCursor().position() == block(4).position(), "deleting a paragraph leaves the next at the caret");
    ed.undo();
    press(ed, Qt::Key_N, Qt::ControlModifier);
    type(ed, "A new paragraph.");
    expect(ed, novel + "A new paragraph.\n");
    ed.undo();
    ed.undo();
    expect(ed, novel);
    ed.loadText("One paragraph.");
    press(ed, Qt::Key_N, Qt::ControlModifier);
    type(ed, "Second.");
    expect(ed, "One paragraph.\nSecond.");
    press(ed, Qt::Key_Y, Qt::ControlModifier);
    expect(ed, "One paragraph.");

    for (const auto &ending : {QStringLiteral("Last heading"), QStringLiteral("Last paragraph.\n")}) {
        ed.loadText(ending);
        ed.jumpTo(ending.size(), false);
        const int pitch = QFontMetrics(ed.font(), ed.viewport()).height();
        const QPointF point(25, ed.cursorRect().bottom() + 1 + 2 * pitch + pitch / 2);
        QMouseEvent click(QEvent::MouseButtonPress, point, point, ed.viewport()->mapToGlobal(point.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(ed.viewport(), &click);
        require(ed.virtualRows() == 3 && !ed.document()->isModified(),
                "virtual writing rows keep body size after a large heading or compact final block");
        ime(ed, "New prose.");
        expect(ed, ending + "\n\n\nNew prose.");
        ed.undo();
        expect(ed, ending);
    }

    ed.loadText("Book//Leaf 1: Haiku #1: prose.\nLeaf 2: Haiku: more.\n");
    ed.replaceRangeLiteral(4, 6, {});
    settle();
    require(ed.model().isGeneric() && rect(2).height() < rect(1).height() / 2 &&
            block(1).layout()->formats().size() == 1 && !bold(1),
            "removing the first-line separator clears crumb styles down the manuscript");
    ed.undo();
    settle();
    require(!ed.model().isGeneric() && rect(2).height() >= rect(1).height() - 2 &&
            block(1).layout()->formats().size() >= 2,
            "undoing a mode change restores crumb typography throughout");
    ed.loadText("Note 1: Haiku #4: prose.\nNote 2: Haiku : more.");
    ed.jumpTo(ed.manuscriptText().lastIndexOf(": more"), false);
    type(ed, "#");
    expect(ed, "Note 1: Haiku #4: prose.\nNote 2: Haiku #: more.");
}

void openingViewport()
{
    for (const int count : {3, 350}) {
        QWidget window;
        QVBoxLayout layout(&window);
        layout.setSizeConstraint(QLayout::SetNoConstraint);
        ManuscriptEditor ed;
        layout.addWidget(&ed);
        // A hidden splitter can still allocate a placeholder-sized editor.
        // Loading positions the real caret before the final newline.
        ed.resize(92, 30);
        QString text = QStringLiteral("Chapters//");
        const QString body = QStringLiteral("A reference to crumb 1. ").repeated(count == 3 ? 1 : 4).trimmed();
        for (int i = 1; i <= count; ++i)
            text += QStringLiteral("Crumb %1: Notes: %2\n").arg(i).arg(body);
        ed.loadText(text);
        window.resize(760, 460);
        window.show();
        ed.setFocus();
        settle();
        const int top = ed.cursorForPosition(QPoint(1, 1)).blockNumber();
        require(ed.textCursor().blockNumber() == count - 1 && ed.textCursor().atBlockEnd(),
                "opening places the caret at the end of the last real crumb");
        require(ed.viewport()->rect().contains(ed.cursorRect()), "opening shows the whole writing caret");
        if (count == 3) {
            require(top == 0 && ed.cursorRect(QTextCursor(ed.document())).top() >= 0,
                    "initial viewport shows the first crumb when the manuscript fits");
        } else {
            require(top < count - 5, "initial viewport shows the end of the preceding page too");
        }
        ed.topCurrentCrumb();
        const QTextCursor header(ed.textCursor().block());
        require(ed.cursorRect(header).top() <= 2, "explicitly pinning the last crumb still works");
        ed.openAtEnd();
        require(ed.cursorForPosition(QPoint(1, 1)).blockNumber() <= (count == 3 ? 0 : count - 5),
                "opening an already visible manuscript restores the writing context");
    }
}

void geometry(ManuscriptEditor &ed)
{
    const auto first = QStringLiteral("Book//Crumb 1: A: ") + QStringLiteral("one more word ").repeated(35).trimmed();
    ed.loadText(first + QStringLiteral("\nCrumb 2: B: done"));
    ed.resize(280,300);
    ed.show();
    ed.setFocus();
    settle();
    ed.jumpTo(0,false);
    const int startY = ed.cursorRect().y();
    press(ed, Qt::Key_Down);
    require(ed.textCursor().blockNumber()==0 && ed.cursorRect().y()>startY, "Down uses wrapped screen lines");
    const auto *wrapped = ed.textCursor().block().layout();
    require(wrapped->lineCount() > 1 &&
            qAbs(wrapped->lineAt(1).y() - wrapped->lineAt(0).y() - wrapped->lineAt(0).height()) <= 1,
            "wrapped rows of the same crumb keep normal line spacing");
    press(ed, Qt::Key_Up);
    const int visual = first.size()/2;
    ed.selectRange(visual-5, visual, false);
    ed.centerCurrentLine();
    require(qAbs(ed.viewport()->mapTo(&ed, ed.cursorRect().center()).y()-ed.contentsRect().center().y())<=2,
            "centering wrapped body preserves exact vertical placement");
    ed.topCurrentCrumb();
    QTextCursor header(ed.document());
    require(ed.cursorRect(header).top() <= 9 && ed.textCursor().position()==visual &&
            ed.textCursor().anchor()==visual-5, "top aligns a wrapped crumb while preserving selection");
    ed.jumpTo(0, false);
    ed.centerCurrentLine();
    const int y = ed.viewport()->mapTo(&ed, ed.cursorRect().center()).y();
    require(qAbs(y-ed.contentsRect().center().y())<=2, "first visual line can be centered");
    ed.openAtEnd();
    ed.topCurrentCrumb();
    require(ed.cursorRect().top() <= 2, "last crumb can reach top at EOF");
    press(ed, Qt::Key_Down);
    press(ed, Qt::Key_Down);
    require(ed.virtualRows()==2 && !ed.document()->isModified(), "virtual rows do not dirty real document");
    const auto before = ed.manuscriptText();
    ime(ed, QStringLiteral("I’d"));
    expect(ed, before + QStringLiteral("\n\nI’d"));
    ed.undo();
    expect(ed, before);
    ed.openAtEnd();
    for (int n=0; n<80; ++n)
        press(ed, Qt::Key_Down);
    require(ed.virtualRows()==80, "arbitrary virtual rows");
    ed.topCurrentCrumb();
    ed.centerCurrentLine();
    press(ed, Qt::Key_Q, Qt::ControlModifier);
    require(ed.virtualRows()==0, "copy on virtual row returns to EOF");
    expect(ed, before);
    ed.loadText(QStringLiteral("Book//Leaf 1: : first\nLeaf 2: : last"));
    const auto endRect = ed.cursorRect();
    const QPointF blank(25, endRect.center().y()+2*endRect.height());
    QMouseEvent click(QEvent::MouseButtonPress, blank, blank,
                      ed.viewport()->mapToGlobal(blank.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ed.viewport(), &click);
    require(ed.virtualRows()==2, "click below last real line moves onto virtual row");
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    require(ed.virtualRows()==0 && ed.textCursor().blockNumber()==1 &&
            ed.textCursor().positionInBlock()==10, "Shift+Tab from virtual space reaches last body");
    ed.loadText(QStringLiteral("Book//Crumb 1: A: ") + QStringLiteral("An ordinary wrapped sentence. ").repeated(3)
                + QStringLiteral("End.\nCrumb 2: B: next"));
    auto paper = ed.palette();
    paper.setColor(QPalette::Base, Qt::white);
    ed.setPaperPalette(paper);
    ed.jumpTo(0,false);
    settle();
    const auto white = ed.viewport()->grab().toImage();
    auto marginColor = [](const QImage &frame, int logicalY) {
        const int pixelY = qFloor((logicalY + 0.5) * frame.devicePixelRatio());
        return frame.pixelColor(frame.width()-3, pixelY);
    };
    const auto band = marginColor(white, ed.cursorRect().center().y());
    require(band.blue()==255 && band.red()>=229 && band.red()<=234 && band.green()>=239 && band.green()<=244,
            "current crumb tint extends to the writing margin");
    QTextCursor lastLine(ed.document()->firstBlock());
    lastLine.movePosition(QTextCursor::EndOfBlock);
    const QRect lastRow = ed.cursorRect(lastLine);
    require(lastRow.top() > ed.cursorRect().top() && marginColor(white, lastRow.center().y()) == band,
            "the current-crumb band covers the last wrapped row too");
    const QRect nextCrumb = ed.cursorRect(QTextCursor(lastLine.block().next()));
    const int gap = nextCrumb.top() - (lastRow.top() + lastRow.height());
    require(gap >= 2 && qAbs(gap - qRound(lastRow.height()/4.0)) <= 2,
            "neighboring crumbs have approximately one quarter of a line of extra space");
    require(ed.cursorForPosition(nextCrumb.center()).blockNumber() == 1,
            "mouse hit testing follows the extra paragraph spacing");
    require(marginColor(white, lastRow.bottom()+1+gap/2) == Qt::white,
            "the current-crumb band ends before the gap");
    ed.setPaperActive(false);
    settle();
    require(marginColor(ed.viewport()->grab().toImage(), ed.cursorRect().center().y())
            == QColor(229,229,229), "inactive window has gray paper");
    ed.setPaperActive(true);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ManuscriptEditor editor;
    punctuation(editor);
    spacing(editor);
    rewritingLastWord(editor);
    wholeWordDeletion(editor);
    deletionClipboard(editor);
    appendUndoPosition(editor);
    undoAndBoundaries(editor);
    editRuns(editor);
    shiftNavigation(editor);
    unicodeRoundTrip(editor);
    fieldsAndClipboard(editor);
    genericParagraphs(editor);
    openingViewport();
    geometry(editor);
    std::puts("Qt editor input, clipboard, layout, virtual space and navigation passed.");
}
