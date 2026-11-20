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
    expect(ed, QStringLiteral("I’d Pepe,” new--"));
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
    expect(ed, QStringLiteral("word --X"));
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

void undoAndBoundaries(ManuscriptEditor &ed)
{
    const QString one = QStringLiteral("Leaf 1: : A");
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

void unicodeRoundTrip(ManuscriptEditor &ed)
{
    // Qt uses a paragraph separator and two otherwise valid noncharacters as
    // internal control markers. In the file only CR or LF begins another crumb.
    const auto prefix = QStringLiteral("Crumb 1: A");
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
    require(QApplication::clipboard()->text() == first, "clipboard restores literal Unicode");
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
    require(ed.textCursor().position() == 8, "Tab reaches first header beyond work title");
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == original.indexOf(QStringLiteral("Haiku")), "Tab reaches title");
    press(ed, Qt::Key_Tab);
    require(ed.textCursor().position() == original.indexOf(QStringLiteral("hóla")), "Tab reaches body");
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    press(ed, Qt::Key_1, Qt::ControlModifier);
    require(ed.textCursor().selectedText() == QStringLiteral("Bóci 1") &&
            QApplication::clipboard()->text() == QStringLiteral("Bóci 1"), "Ctrl+1 copies custom header without colon");
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

    ed.loadText(QStringLiteral("Title // Note 1: A: x\n"));
    ed.deleteCrumb();
    expect(ed, QStringLiteral("Title //\n"));
    ed.appendCrumb();
    expect(ed, QStringLiteral("Title //Note 1: : \n"));
    ed.loadText(QStringLiteral("Note 1: Haiku #4: x\nNote 2: Haiku : y\n"));
    ed.jumpTo(ed.manuscriptText().lastIndexOf(QStringLiteral(": y")), false);
    type(ed, QStringLiteral("#"));
    expect(ed, QStringLiteral("Note 1: Haiku #4: x\nNote 2: Haiku #5: y\n"));
    ed.undo();
    expect(ed, QStringLiteral("Note 1: Haiku #4: x\nNote 2: Haiku : y\n"));
    ed.appendCrumb(QStringLiteral("Haiku"));
    expect(ed, QStringLiteral("Note 1: Haiku #4: x\nNote 2: Haiku : y\nNote 3: Haiku #5: \n"));
    press(ed, Qt::Key_Up, Qt::ControlModifier);
    ed.undo();
    expect(ed, QStringLiteral("Note 1: Haiku #4: x\nNote 2: Haiku : y\n"));
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
        QString text;
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
    const auto first = QStringLiteral("Crumb 1: A: ") + QStringLiteral("one more word ").repeated(35).trimmed();
    ed.loadText(first + QStringLiteral("\nCrumb 2: B: done"));
    ed.resize(280,300);
    ed.show();
    ed.setFocus();
    settle();
    ed.jumpTo(0,false);
    const int startY = ed.cursorRect().y();
    press(ed, Qt::Key_Down);
    require(ed.textCursor().blockNumber()==0 && ed.cursorRect().y()>startY, "Down uses wrapped screen lines");
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
    ed.loadText(QStringLiteral("Leaf 1: : first\nLeaf 2: : last"));
    const auto endRect = ed.cursorRect();
    const QPointF blank(25, endRect.center().y()+2*endRect.height());
    QMouseEvent click(QEvent::MouseButtonPress, blank, blank,
                      ed.viewport()->mapToGlobal(blank.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ed.viewport(), &click);
    require(ed.virtualRows()==2, "click below last real line moves onto virtual row");
    press(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    require(ed.virtualRows()==0 && ed.textCursor().blockNumber()==1 &&
            ed.textCursor().positionInBlock()==10, "Shift+Tab from virtual space reaches last body");
    ed.loadText(QStringLiteral("Crumb 1: Short: paragraph\nCrumb 2: B: next"));
    auto paper = ed.palette();
    paper.setColor(QPalette::Base, Qt::white);
    ed.setPaperPalette(paper);
    ed.jumpTo(0,false);
    settle();
    const auto white = ed.viewport()->grab().toImage();
    const auto band = white.pixelColor(white.width()-3, ed.cursorRect().center().y());
    require(band.blue()==255 && band.red()>=229 && band.red()<=234 && band.green()>=239 && band.green()<=244,
            "current crumb tint extends to the writing margin");
    ed.setPaperActive(false);
    settle();
    require(ed.viewport()->grab().toImage().pixelColor(white.width()-3, ed.cursorRect().center().y())
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
    undoAndBoundaries(editor);
    unicodeRoundTrip(editor);
    fieldsAndClipboard(editor);
    openingViewport();
    geometry(editor);
    std::puts("Qt editor input, clipboard, layout, virtual space and navigation passed.");
}
