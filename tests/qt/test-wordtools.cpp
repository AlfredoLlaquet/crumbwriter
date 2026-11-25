#include "manuscripteditor.h"
#include "thesaurus.h"
#include "wordtools.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool valid, const char *message)
{
    if (valid) return;
    std::fprintf(stderr, "Word tools failed: %s\n", message);
    std::abort();
}

void press(QWidget *widget, Qt::Key code, Qt::KeyboardModifiers modifiers = {})
{
    QKeyEvent down(QEvent::KeyPress, code, modifiers);
    QApplication::sendEvent(widget, &down);
    QKeyEvent up(QEvent::KeyRelease, code, modifiers);
    QApplication::sendEvent(widget, &up);
    QApplication::processEvents();
}

// Capture the real desktop-services request without launching a browser.
class TestBrowser final : public QTextBrowser {
public:
    QUrl requested;
protected:
    void doSetSource(const QUrl &url, QTextDocument::ResourceType) override { requested = url; }
};

void testWords(ManuscriptEditor &editor, WordTools &tools)
{
    editor.loadText(QStringLiteral("‘I’m’ reading Tsa-shan-nix, co‑author and fiancé."));
    const auto text = editor.manuscriptText();
    for (const QString &word : QStringList{"I’m", "Tsa-shan-nix", "co‑author", "fiancé"}) {
        const int start = int(text.indexOf(word));
        for (int caret : {start, start + 1, start + int(word.size())}) {
            editor.jumpTo(caret, false);
            tools.selectWord();
            require(editor.textCursor().selectedText() == word, "selects full word, including internal punctuation");
        }
    }
    using Ref = WordTools::Reference;
    require(WordTools::referenceUrl(Ref::Dictionary, "  doctor  ").toEncoded() ==
            "https://www.merriam-webster.com/dictionary/doctor", "dictionary address");
    require(WordTools::referenceUrl(Ref::Synonyms, "doctor").toEncoded() ==
            "https://www.merriam-webster.com/dictionary/doctor#synonyms", "dictionary synonyms anchor");
    require(WordTools::referenceUrl(Ref::Thesaurus, QStringLiteral("a/b #café?")).toEncoded() ==
            "https://www.merriam-webster.com/thesaurus/a%2Fb%20%23caf%C3%A9%3F", "one safely encoded URL segment");
    require(WordTools::referenceUrl(Ref::Dictionary, " \n ").isEmpty(), "blank selection has no lookup URL");
    TestBrowser browser;
    QDesktopServices::setUrlHandler("https", &browser, "setSource");
    editor.jumpTo(int(text.indexOf("I’m")) + 1, false);
    tools.openWeb(Ref::Dictionary);
    require(browser.requested == WordTools::referenceUrl(Ref::Dictionary, "I’m"), "web lookup uses caret word");
    editor.selectRange(0, int(text.indexOf(" reading")), false);
    tools.openWeb(Ref::Synonyms);
    require(browser.requested == WordTools::referenceUrl(Ref::Synonyms, "‘I’m’"), "web lookup uses exact selection");
    QDesktopServices::unsetUrlHandler("https");
}

void testManuscriptSelection(ManuscriptEditor &editor, WordTools &tools)
{
    const QString source = QStringLiteral("notwithstanding—not O’Brien co‑author re-enter cafe\u0301 🦆.");
    editor.resize(900, 230);
    editor.loadText(source);
    QApplication::processEvents();
    for (const auto &word : QStringList{"notwithstanding", "—", "not", "O’Brien",
                                        "co‑author", "re-enter", QStringLiteral("cafe\u0301"), "🦆"}) {
        // 'not' also starts the first word. Target the independent final word.
        const int start = int(word == "not" ? source.indexOf(QStringLiteral("—not")) + 1 : source.indexOf(word));
        QTextCursor glyph(editor.document());
        glyph.setPosition(start);
        const QRect left = editor.cursorRect(glyph);
        glyph.movePosition(QTextCursor::NextCharacter);
        const QRect right = editor.cursorRect(glyph);
        const QPoint point((left.x() + right.x()) / 2, left.center().y());
        QMouseEvent click(QEvent::MouseButtonDblClick, point, editor.viewport()->mapToGlobal(point),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &click);
        require(editor.textCursor().selectedText() == word, "double-click selects the printed word or grapheme");
        if (word == QStringLiteral("🦆")) continue;
        editor.jumpTo(start, false);
        tools.selectWord();
        require(editor.textCursor().selectedText() == word, "keyboard word selection respects the em dash");
    }
}

void testDialog(ManuscriptEditor &editor, WordTools &tools)
{
    editor.loadText("Ask the doctor tomorrow.");
    editor.jumpTo(9, false);
    tools.showThesaurus();
    QApplication::processEvents();
    auto *dialog = editor.window()->findChild<QDialog *>("offlineThesaurus");
    require(dialog && dialog->isVisible() && !dialog->isModal(), "modeless window");
    auto *query = dialog->findChild<QLineEdit *>("thesaurusQuery");
    auto *tree = dialog->findChild<QTreeWidget *>("thesaurusResults");
    auto *replace = dialog->findChild<QPushButton *>("thesaurusReplace");
    require(query && tree && replace && query->text() == "doctor", "uses word at caret");
    require(tree->topLevelItemCount() >= 2 && tree->currentItem()->parent(), "results grouped by meaning");
    auto *initial = tree->currentItem();
    press(tree, Qt::Key_Down);
    require(tree->currentItem() != initial && tree->currentItem()->parent(), "arrow advances to another alternative");
    const auto matches = tree->findItems("physician", Qt::MatchExactly | Qt::MatchRecursive);
    require(!matches.isEmpty(), "real English data returns physician for doctor");
    tree->setCurrentItem(matches.first());
    press(tree, Qt::Key_C, Qt::ControlModifier);
    require(QApplication::clipboard()->text() == "physician" && dialog->isVisible(), "copy leaves dialog open");
    editor.replaceRangeLiteral(0, 0, "Please ");
    press(tree, Qt::Key_Return);
    require(!dialog->isVisible() && editor.manuscriptText() == "Please Ask the physician tomorrow.",
            "replacement follows word when preceding text moves");
    editor.undo();
    require(editor.manuscriptText() == "Please Ask the doctor tomorrow.", "one undo restores source");

    editor.selectRange(7, 14, false);
    tools.showThesaurus();
    require(query->text() == "Ask the", "explicit phrase takes priority");
    query->setText("doctor");
    require(replace->isEnabled(), "can query a different word for the selected phrase");
    editor.replaceRangeLiteral(7, 14, "Send");
    require(!replace->isEnabled(), "editing target while browsing invalidates replacement");
    const auto changed = editor.manuscriptText();
    press(tree, Qt::Key_Return);
    require(editor.manuscriptText() == changed, "stale target leaves manuscript intact");
    press(tree, Qt::Key_Escape);
    require(!dialog->isVisible(), "Escape closes without modifying document");

    editor.loadText("");
    tools.showThesaurus();
    query->setText("DOCTOR");
    require(tree->topLevelItemCount() > 0 && !replace->isEnabled(), "can browse without a target");
    query->setText("qwxzunknown");
    require(tree->topLevelItemCount() == 0, "unknown words show no stale suggestions");
    tools.closeThesaurus();
}
}

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QApplication app(argc, argv);
    ManuscriptEditor editor;
    editor.show();
    WordTools tools(&editor);
    const auto &thesaurus = EnglishThesaurus::instance();
    require(thesaurus.available(), "American English dictionary is installed");
    require(thesaurus.lookup("doctor’s bill").size() == thesaurus.lookup("doctor's bill").size() &&
            !thesaurus.lookup("doctor’s bill").isEmpty(), "curly apostrophe matches the index");
    testWords(editor, tools);
    testManuscriptSelection(editor, tools);
    testDialog(editor, tools);
    std::puts("Word selection, Merriam-Webster addresses and offline thesaurus passed.");
}
