#include "manuscripteditor.h"
#include "searchengine.h"
#include "searchhistory.h"
#include "searchpanel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstdio>
#include <cstdlib>

namespace {

void check(bool pass, const char *message)
{
    if (!pass) {
        std::fprintf(stderr, "Search check failed: %s\n", message);
        std::exit(1);
    }
}

template<class Condition> void waitFor(Condition done, const char *message)
{
    QElapsedTimer timeout;
    timeout.start();
    while (!done() && timeout.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    check(done(), message);
}

QString apply(const QString &input, const cwsearch::ReplacementPlan &plan)
{
    QString result = input;
    for (auto it = plan.edits.crbegin(); it != plan.edits.crend(); ++it)
        result.replace(it->original.start, it->original.end - it->original.start, it->replacement);
    return result;
}

void regexEngine()
{
    const std::atomic_bool active{false};
    auto clean = [](QString s) { return ManuscriptEditor::normalizeInsertion(std::move(s)); };
    const QString input = QStringLiteral("😀 a1b2b\ncatfish underdog Cat dog\nA\u00a0B\n");
    cwsearch::Options opts;
    opts.mode = cwsearch::Mode::Regex;
    auto regex = [&](const QString &query) { return cwsearch::Pattern(query, opts); };
    auto shortMatches = regex(QStringLiteral("a.+b")).collect(input, active).spans;
    check(shortMatches.size() == 2 && shortMatches[0] == cwsearch::Span{3, 6}, "ungreedy Unicode offsets");
    auto longMatches = regex(QStringLiteral("(?-U)a.+b")).collect(input, active).spans;
    check(longMatches[0] == cwsearch::Span{3, 8}, "inline greedy override");
    opts.wholeWords = true;
    auto words = regex(QStringLiteral("(*UTF)cat|dog")).collect(input, active).spans;
    check(words.size() == 2 && input.mid(words[0].start, 3) == QStringLiteral("Cat"),
          "whole-word alternation, UTF start directive, case folding");
    opts.wholeWords = false;
    auto captures = regex(QStringLiteral("(?<a>a)(\\d)(b)"));
    auto plan = captures.plan(input, QStringLiteral("\\3-\\2-\\g<a>"), clean, active);
    check(plan.error.isEmpty() && plan.affectedLines == 1 && plan.changes == 1 &&
          apply(input, plan).startsWith(QStringLiteral("😀 b-1-a2b")), "named and numbered replacements");
    check(apply(input, plan).contains(QStringLiteral("A\u00a0B")), "unaffected NBSP");
    auto invalid = captures.plan(input, QStringLiteral("\\g<"), clean, active);
    check(!invalid.error.isEmpty() && invalid.edits.isEmpty(), "invalid replacement has no edits");
    check(!regex(QStringLiteral("[")).error().isEmpty(), "PCRE diagnostic");
    auto merged = regex(QStringLiteral("dog\\nA")).plan(input, QStringLiteral("join"), clean, active);
    check(merged.affectedLines == 2 && apply(input, merged).contains(QStringLiteral("Cat join\u00a0B")),
          "multiple source crumbs in a multiline replacement");
    const QString stems = QStringLiteral("A a A\nA a\n");
    auto casePlan = regex(QStringLiteral("a")).plan(stems, QStringLiteral("a"), clean, active);
    check(casePlan.affectedLines == 2 && casePlan.changes == 3 && casePlan.examples.size() == 2 &&
          apply(stems, casePlan) == QStringLiteral("a a a\na a\n"), "preview actual changed lines and matches");
    check(casePlan.examples.first().beforeMatches == QVector<cwsearch::Span>({{0, 1}, {4, 5}}) &&
          casePlan.examples.last().beforeMatches == QVector<cwsearch::Span>({{0, 1}}),
          "preview marks each exact source match, leaving identical unchanged text in normal type");
    auto unchanged = regex(QStringLiteral("a")).plan(QStringLiteral("a"), QStringLiteral("a"), clean, active);
    check(unchanged.edits.isEmpty() && !unchanged.changes, "no-op preview");
    auto look = regex(QStringLiteral("(?<=😀)(?=x)")).plan(QStringLiteral("😀x"), QStringLiteral("X"), clean, active);
    check(apply(QStringLiteral("😀x"), look) == QStringLiteral("😀Xx"), "zero-width capture at Unicode boundary");
}

void historyImport()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "create legacy settings");
    file.write("[search]\nrecent=[(uint32 16, 'old plain'), (2, '7'), (34, '9')]\n");
    file.close();
    QSettings settings(path, QSettings::IniFormat);
    cwsearch::History history(&settings);
    auto entries = history.entries();
    check(entries.size() == 3 && entries[0].query == QStringLiteral("old plain") &&
          entries[1].options.wrap && !entries[2].options.wrap, "legacy GVariant history import");
    cwsearch::Options options;
    options.mode = cwsearch::Mode::Regex;
    history.remember({QStringLiteral("é,\\w+"), options});
    cwsearch::History reopened(&settings);
    reopened.remember({QStringLiteral("old plain"), {}});
    entries = history.entries();
    check(entries.size() == 4 && entries[0].query == QStringLiteral("old plain") &&
          entries[1].query == QStringLiteral("é,\\w+") && entries[1].options.mode == cwsearch::Mode::Regex,
          "deduplicated ordered history across windows");
}

class Fixture {
public:
    QTemporaryDir directory;
    QSettings settings{directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
    QWidget window;
    ManuscriptEditor *editor = new ManuscriptEditor;
    SearchPanel *search = new SearchPanel(editor, &settings);
    QLineEdit *query;
    QLineEdit *number;
    QLineEdit *replacement;
    int returns = 0;
    int lastOrigin = -1;

    Fixture()
    {
        editor->setCopyDeletedText(true);
        auto *layout = new QVBoxLayout(&window);
        layout->addWidget(search);
        layout->addWidget(editor);
        query = search->findChild<QLineEdit *>(QStringLiteral("cw-find"));
        number = search->findChild<QLineEdit *>(QStringLiteral("cw-crumb-number"));
        replacement = search->findChild<QLineEdit *>(QStringLiteral("cw-replacement"));
        search->recordOrigin = [this](const QTextCursor &cursor) {
            ++returns;
            lastOrigin = cursor.position();
        };
        window.resize(620, 400);
        window.show();
        window.activateWindow();
        editor->setFocus();
        // X11 delivers activation after the new toplevel maps. Subsequent
        // field-focus assertions exercise the UI only once it owns input.
        waitFor([&] { return editor->hasFocus(); }, "fixture manuscript owns keyboard focus");
    }

    bool key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, k, m);
        return search->handleKey(&event);
    }
    QString status() const
    {
        return search->findChild<QLabel *>(QStringLiteral("cw-search-status"))->text();
    }
    QPushButton *button(const char *name) const
    {
        return search->findChild<QPushButton *>(QString::fromLatin1(name));
    }
    bool isAt(int row, const QString &selected = {}) const
    {
        const auto cursor = editor->textCursor();
        return cursor.blockNumber() == row && cursor.selectedText() == selected;
    }
};

void anchoredNavigation()
{
    Fixture f;
    const QString input = QStringLiteral("è needle\nold nest\nanchor\nnew nest\nnext needle\nlast needle");
    f.editor->loadText(input);
    const int start = input.indexOf(QStringLiteral("anchor")) + 3;
    f.editor->jumpTo(start);
    f.search->find(true);
    f.query->setText(QStringLiteral("ne"));
    check(f.isAt(1, QStringLiteral("ne")), "previous preview starts at caret");
    f.query->setText(QStringLiteral("needle"));
    check(f.isAt(0, QStringLiteral("needle")), "refined query starts from original caret");
    f.query->setText(QStringLiteral("absent"));
    check(f.editor->textCursor().position() == start && !f.editor->textCursor().hasSelection(),
          "no match restores origin");
    f.query->setText(QStringLiteral("needle"));
    check(f.returns == 1 && f.lastOrigin == start, "one Back origin per live search");
    check(!f.settings.contains(QStringLiteral("search/history-v2")), "exclude intermediate queries from history");
    f.key(Qt::Key_F3);
    check(f.isAt(5, QStringLiteral("needle")), "backward F3 wraps");
    f.key(Qt::Key_F3, Qt::ShiftModifier);
    check(f.isAt(0, QStringLiteral("needle")), "Shift F3 reverses chosen direction once");
    f.key(Qt::Key_Escape);
    check(f.search->isHidden() && !f.editor->textCursor().hasSelection() && f.editor->textCursor().position() == 2,
          "Escape collapses at highlighted match start");
    f.key(Qt::Key_F3);
    check(f.isAt(5, QStringLiteral("needle")) && f.search->isHidden(), "F3 after Escape");
    f.editor->jumpTo(start);
    f.search->find(false);
    f.query->setText(QStringLiteral("ne"));
    check(f.isAt(3, QStringLiteral("ne")), "opening Ctrl F resets direction and origin");
    f.editor->jumpTo(0);
    f.query->setText(QStringLiteral("needle"));
    check(f.isAt(0, QStringLiteral("needle")), "manual caret movement establishes new origin");
}

void overlappingSearch()
{
    Fixture f;
    const QString word = QStringLiteral("bananana");
    for (const QString &paper : {word, word + QString(40000, u'z')}) {
        f.search->reset();
        f.editor->loadText(paper);
        f.editor->jumpTo(2, false);
        f.search->find();
        f.query->setText(QStringLiteral("ana"));
        waitFor([&] { return f.editor->textCursor().position() == 5 && f.status().contains(QStringLiteral("of 2")); },
                "overlapping query uses same occurrences for count, preview and navigation");
        f.search->repeat();
        check(f.editor->textCursor().position() == 1, "overlapping query wraps to first occurrence");
    }
}

void directFindAndFocus()
{
    Fixture f;
    const QString paper = QStringLiteral("old code1 code2\nanchor co.e\nnew code3 code4");
    const int middle = paper.indexOf(u"anchor");
    f.editor->loadText(paper);
    f.editor->jumpTo(middle);
    auto *mode = f.search->findChild<QToolButton *>("cw-search-mode");
    auto *words = f.search->findChild<QCheckBox *>("cw-search-word");
    auto *wrap = f.search->findChild<QCheckBox *>("cw-search-wrap");

    f.key(Qt::Key_G, Qt::ControlModifier);
    f.key(Qt::Key_R, Qt::ControlModifier);
    f.query->setText("code[0-9]");
    f.search->repeat();
    check(f.isAt(0, "code2"), "switching to regex in a backward search preserves its direction");
    f.key(Qt::Key_Escape);

    const int seed = paper.indexOf(u"co.e");
    f.editor->selectMatch(seed, seed + 4);
    check(f.key(Qt::Key_R, Qt::ControlModifier) && f.query->hasFocus() &&
          mode->text() == "Regex" && f.query->text() == "co.e",
          "Ctrl R on a manuscript selection opens regex with the selected expression");
    f.query->setText("code[0-9]");
    f.search->repeat();
    check(f.isAt(2, "code3"), "direct regex search starts forward from the writing caret");

    f.editor->jumpTo(middle);
    f.editor->setFocus();
    check(!f.key(Qt::Key_W, Qt::ControlModifier) && !f.key(Qt::Key_W, Qt::AltModifier) &&
          !words->isChecked() && wrap->isChecked(), "editing shortcuts stay with manuscript when find is open");
    check(f.key(Qt::Key_P, Qt::ControlModifier) && f.query->hasFocus() && mode->text() == "Text",
          "Ctrl P opens plain-text search directly from the manuscript");
    f.query->setText("code");
    check(f.isAt(2, "code"), "direct text search previews the first match after the writing caret");
    check(f.key(Qt::Key_W, Qt::ControlModifier) && f.key(Qt::Key_W, Qt::AltModifier) &&
          words->isChecked() && !wrap->isChecked(), "whole-word and wrap shortcuts stay in the search fields");

    f.key(Qt::Key_H, Qt::ControlModifier);
    f.key(Qt::Key_R, Qt::ControlModifier);
    check(f.replacement->isVisible(), "switching modes from a search field preserves Replace");
    check(!f.key(Qt::Key_N, Qt::ControlModifier), "Ctrl N is available to append from an open search");
    f.key(Qt::Key_L, Qt::ControlModifier);
    check(f.number->hasFocus() && !f.replacement->isVisible(), "Ctrl L opens number mode from Replace");
    f.key(Qt::Key_Escape);
    check(f.key(Qt::Key_P, Qt::ControlModifier) && f.query->hasFocus() && mode->text() == "Text",
          "Ctrl P opens plain text when Find is closed");
}

void controlsAndReplace()
{
    Fixture f;
    const QString text = QStringLiteral("Title//Leaf 9: A: word 3.\nLeaf 5: B: word 12.\nLeaf 9: C: \u00a0.\n");
    f.editor->loadText(text);
    f.editor->jumpTo(text.indexOf(QStringLiteral("Leaf 5")) + 2);
    f.key(Qt::Key_G, Qt::ControlModifier);
    f.editor->setFocus();
    f.key(Qt::Key_L, Qt::ControlModifier);
    f.number->setText(QStringLiteral("9"));
    f.search->repeat();
    check(f.search->isHidden() && f.editor->textCursor().position() == text.indexOf(QStringLiteral("Leaf 9")),
          "Ctrl L in an open backward search preserves direction even after focusing the manuscript");
    f.search->goToNumber();
    f.number->setText(QStringLiteral("5"));
    f.key(Qt::Key_H, Qt::ControlModifier);
    waitFor([&] { return f.query->hasFocus() && f.replacement->isVisible() && !f.number->isVisible(); },
            "Ctrl H from number opens text replace");
    f.key(Qt::Key_R, Qt::ControlModifier);
    f.query->setText(QStringLiteral("word (?-U:([0-9]+))"));
    f.replacement->setText(QStringLiteral("entry   \\1"));
    f.key(Qt::Key_H, Qt::ControlModifier);
    check(f.replacement->hasFocus(), "second Ctrl H focuses Replace with");
    f.key(Qt::Key_H, Qt::ControlModifier);
    check(f.query->hasFocus() && f.replacement->text() == QStringLiteral("entry   \\1"), "third Ctrl H returns to Find");
    auto *all = f.button("cw-replace-all");
    check(all->isEnabled(), "replace all available for regex");
    QApplication::clipboard()->setText("previous clipboard");
    all->click();
    auto *preview = f.search->findChild<QPlainTextEdit *>(QStringLiteral("cw-replace-preview"));
    check(!preview->isHidden() && preview->toPlainText().contains(QStringLiteral("2 crumbs")) &&
          preview->toPlainText().contains(QStringLiteral("entry 12")) && f.editor->manuscriptText() == text,
          "preview has affected count and normalized examples without modifying manuscript");
    const auto previewText = preview->toPlainText();
    QTextCursor beforeMatch(preview->document());
    beforeMatch.setPosition(previewText.indexOf(QStringLiteral("word 12")) + 1);
    check(beforeMatch.charFormat().fontWeight() == QFont::Bold, "the source match is bold in the rendered preview");
    beforeMatch.setPosition(previewText.indexOf(QStringLiteral("entry 12")) + 1);
    check(beforeMatch.charFormat().fontWeight() != QFont::Bold, "generated replacement text keeps normal weight");
    f.replacement->setText(QStringLiteral("other \\1"));
    check(preview->isHidden(), "editing replacement invalidates stale confirmation");
    check(QApplication::clipboard()->text() == "previous clipboard", "preview does not count as a deletion");
    all->click();
    all->click();
    check(f.editor->manuscriptText() == QString(text).replace(QStringLiteral("word"), QStringLiteral("other")),
          "confirmed regex bulk replacement preserves Unicode, headers, terminal newline");
    check(QApplication::clipboard()->text() == "word 3\nword 12",
          "bulk replacement copies complete changed matches in manuscript order");
    f.editor->undo();
    check(f.editor->manuscriptText() == text, "one Undo restores all replacements");
    f.key(Qt::Key_L, Qt::ControlModifier);
    check(!f.replacement->isVisible() && !all->isVisible(), "no replacements in number mode");
    f.key(Qt::Key_P, Qt::ControlModifier);
    f.query->setText(QStringLiteral("word"));
    f.key(Qt::Key_Down, Qt::AltModifier);
    auto *list = f.search->findChild<QListView *>(QStringLiteral("cw-recent-list"));
    check(list->isVisible() && list->model()->rowCount() >= 2, "recent query list with modes");
}

void staleAndReservedText()
{
    Fixture f;
    const QString source = QStringLiteral("First passage: A\u2029B.\nSecond passage: A\u2029B.");
    f.editor->loadText(source);
    const int at = source.indexOf(QStringLiteral("A\u2029B"));
    f.editor->selectRange(at, at + 3);
    f.search->findReplace();
    check(f.query->text() == QStringLiteral("A\u2029B"), "physical-line selection preserves a literal paragraph separator");
    f.replacement->setText(QStringLiteral("x\ufdd0\u2029y"));
    auto *all = f.button("cw-replace-all");
    all->click();
    auto *preview = f.search->findChild<QPlainTextEdit *>(QStringLiteral("cw-replace-preview"));
    check(preview->isVisible() && preview->toPlainText().contains("2 paragraphs"), "initial two-paragraph preview");
    f.editor->replaceRangeLiteral(at, at + 3, QStringLiteral("edited"));
    check(!preview->isVisible(), "manuscript edits invalidate a displayed confirmation immediately");
    waitFor([&] { return all->isEnabled(); }, "updated match count after manuscript edit");
    all->click();
    check(preview->toPlainText().startsWith(QStringLiteral("1 replacement in 1 paragraph")),
          "new preview shows revised count");
    all->click();
    check(f.editor->blockCount() == 2 && f.editor->manuscriptText().endsWith(QStringLiteral("x\ufdd0\u2029y.")),
          "bulk replacement retains reserved Qt characters as literal Unicode");
    f.editor->undo();
    check(f.editor->manuscriptText().endsWith(QStringLiteral("A\u2029B.")) &&
          f.editor->manuscriptText().startsWith(QStringLiteral("First passage: edited.")),
          "one undo restores the bulk edit without changing earlier manual work");
}

void asynchronousSearch()
{
    Fixture f;
    const QString large = QStringLiteral("Crumb 1: : padding.\n").repeated(6500) +
                          QStringLiteral("needle one\nneedle two\nneedle three");
    f.editor->loadText(large);
    f.editor->jumpTo(0, false);
    f.search->find(true);
    f.key(Qt::Key_W, Qt::AltModifier);
    f.query->setText(QStringLiteral("nee"));
    f.query->setText(QStringLiteral("needle"));
    f.key(Qt::Key_F3, Qt::ShiftModifier);
    check(f.isAt(6500, QStringLiteral("needle")), "large forward repeat during pending backward scan");
    f.key(Qt::Key_R, Qt::ControlModifier);
    f.query->setText(QStringLiteral("needl[ex]"));
    f.key(Qt::Key_F3, Qt::ShiftModifier);
    waitFor([&] { return f.isAt(6500, QStringLiteral("needle")) && !f.status().contains(QStringLiteral("Searching")); },
            "queued regex navigation survives fresh background index");
    f.query->setText(QStringLiteral("absent"));
    f.search->closeSearch();
    f.query->setText(QStringLiteral("padding"));
    // Destruction while a superseded match scan is running must stay safe.
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    regexEngine();
    historyImport();
    anchoredNavigation();
    overlappingSearch();
    directFindAndFocus();
    controlsAndReplace();
    staleAndReservedText();
    asynchronousSearch();
    std::puts("Qt search, regex, history, navigation and replacement checks passed");
}
