#include "crumbmodel.h"

#include <QtCore/QFile>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>

#include <clocale>
#include <cstdio>
#include <random>
#include <set>

#define CHECK(condition) do { if (!(condition)) qFatal("%s:%d: %s", __FILE__, __LINE__, #condition); } while (false)

static void replace(QTextDocument &doc, int start, int length, const QString &text)
{
    QTextCursor cursor(&doc);
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

static void fieldsAndWords()
{
    QTextDocument doc;
    doc.setPlainText(QString::fromUtf8("📚 Book//  Bocí 1: M🌀 (Éloge #1): cité — word / second\n"
                                      "Bocí 2: E\u0301LOGE #2, Straße #1: x\n"
                                      "Bocí 3: STRASSE #3: x\n"
                                      "Bocí 4: Empty title coming\n"
                                      "Bocí 000000000000000000000: : incorrect\n"
                                      "partial\n"));
    CrumbModel model(&doc);
    const auto first = model.record(0);
    CHECK(first.valid && first.number == 1 && first.label == QStringLiteral("Bocí"));
    CHECK(first.text.mid(first.headerStart, first.headerEnd-first.headerStart) == QStringLiteral("Bocí 1"));
    CHECK(first.text.mid(first.titleStart, first.titleEnd-first.titleStart) == QStringLiteral("M🌀 (Éloge #1)"));
    CHECK(first.text.mid(first.bodyStart) == QStringLiteral("cité — word / second"));
    CHECK(first.text.left(first.recordStart) == QStringLiteral("📚 Book//"));
    CHECK(first.words == 8);
    CHECK(first.genres.size() == 1 && first.text.at(first.genres[0].start) == u'1');
    CHECK(first.warnings.isEmpty() && model.record(1).warnings.isEmpty());
    CHECK(model.record(2).warnings.size() == 1 && model.record(2).warnings[0].expected == u"2");
    CHECK(!model.record(4).valid && model.record(4).headerWarning);
    CHECK(model.record(4).warnings.first().actual == u"000000000000000000000");
    CHECK(!model.record(5).valid && model.record(5).warnings.isEmpty());
    CHECK(model.record(3).bodyStart == model.record(3).text.size());
    CHECK(model.genres().size() == 2);
    CHECK(model.nextGenreTitle(QStringLiteral("strasse")) == QStringLiteral("STRASSE #4"));
    CHECK(model.nextGenreTitle(QStringLiteral("éloge")) == QStringLiteral("E\u0301LOGE #3"));
    CHECK(model.nextHeader() == u"Bocí 5: : ");
    CHECK(model.sessionCount() == 0);
}

static void numbersAndSession()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Book// Crumb 7: Alpha: x\npartial\nNote 7: Beta: y\nEntry 3: Z: z"));
    CrumbModel model(&doc);
    const int a = model.record(0).headerStart;
    const int b = doc.findBlockByNumber(2).position();
    CHECK(model.findNumber(7, 0, false, false) == a);
    CHECK(model.findNumber(7, a, false, false, false) == b);
    CHECK(model.findNumber(7, b, true, false, false) == a);
    CHECK(model.findNumber(7, a, true, true, false) == b);
    CHECK(model.findNumber(7, a-1, true, false) == -1);
    CHECK(model.findNumber(99, 0, false, true) == -1);
    CHECK(model.neighbor(0, false) == 2 && model.neighbor(2, true) == 0);
    CHECK(model.nextHeader(QStringLiteral("Title")) == u"Entry 8: Title: ");

    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("\nPip 8: E: w\nPip 8: F: h"));
    CHECK(model.sessionCount() == 1 && model.nextHeader() == u"Pip 9: : ");
    doc.undo();
    CHECK(model.sessionCount() == 0 && model.nextHeader() == u"Entry 8: : ");
    doc.redo();
    CHECK(model.sessionCount() == 1);
    model.markOpened();
    CHECK(model.sessionCount() == 0);
    replace(doc, 0, doc.characterCount()-1, {});
    CHECK(model.nextHeader() == u"Pip 1: : ");
    model.markOpened();
    CHECK(model.nextHeader() == u"Crumb 1: : ");
}

static void genreEditsAndWarnings()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("C 1: Scene #1, SCENE #3: body\nC 2: scEne #4: body\n"
                                    "C 3: Fingers (Scene #): body\nC 5: sCene #99: body"));
    CrumbModel model(&doc);
    const int hash = int(model.record(2).text.indexOf(u'#'));
    CHECK(model.completeGenre(2, hash) == u"5");
    CHECK(model.completeGenre(2, hash+1).isEmpty());
    CHECK(model.record(0).warnings.size() == 1 && model.record(0).warnings[0].actual == u"3");
    const int before = int(model.record(0).text.indexOf(u'3'));
    CrumbChange seen;
    model.observe(&doc, [&](const CrumbChange &c) { seen = c; });
    replace(doc, before, 1, QStringLiteral("2"));
    CHECK(model.record(0).warnings.isEmpty());
    CHECK(model.record(1).warnings.size() == 1 && model.record(1).warnings[0].expected == u"3");
    CHECK(seen.otherLines.contains(1));
    CHECK(model.record(3).warnings.size() == 2 && model.record(3).warnings.first().expected == u"4");
    auto warning = model.nextWarning(0, false, false);
    CHECK(warning && warning->line == 1 && warning->genre == u"scEne");
    const int secondWarning = doc.findBlockByNumber(1).position() + warning->start;
    CHECK(!model.nextWarning(secondWarning, true, false));
    CHECK(model.nextWarning(secondWarning, true)->actual == u"99");
    const auto next = model.nextWarning(secondWarning, false, false);
    CHECK(next && next->line == 3 && next->genre.isEmpty() && next->actual == u"5");
    const auto after = model.nextWarning(doc.findBlockByNumber(3).position() + next->start, false);
    CHECK(after && after->line == 3 && after->actual == u"99");

    QTextCursor cursor(&doc);
    cursor.setPosition(doc.findBlockByNumber(2).position() + hash);
    cursor.deleteChar();
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral("#"));
    CHECK(model.completeGenre(2, hash) == u"5");
    cursor.endEditBlock();

    doc.setPlainText(QStringLiteral("C 1: Scene #18446744073709551615: a\n"
                                    "C 2: Scene #1: b\nC 3: Scene #18446744073709551616: c\n"
                                    "C 18446744073709551616: Overflow: c\n"
                                    "C 18446744073709551615: Max: d"));
    CHECK(model.record(1).warnings.first().expected == u"no higher number");
    CHECK(model.record(2).genres.first().overflow && !model.genres().first().canAppend);
    CHECK(!model.record(3).valid && model.record(3).headerWarning);
    CHECK(model.nextHeader().isEmpty() && model.nextGenreTitle(QStringLiteral("Scene")).isEmpty());
}

static void observersAndFormatting()
{
    QPlainTextEdit view;
    view.setPlainText(QStringLiteral("Crumb 1: Title: text"));
    auto &doc = *view.document();
    CrumbModel model(&doc);
    int textChanges = 0, metadataChanges = 0;
    model.observe(&doc, [&](const CrumbChange &change) {
        if (change.textChanged)
            ++textChanges;
        else
            ++metadataChanges;
    });
    QTextCursor cursor(&doc);
    cursor.select(QTextCursor::Document);
    QTextCharFormat style;
    style.setFontItalic(true);
    cursor.mergeCharFormat(style);
    CHECK(textChanges == 0);
    model.markOpened();
    CHECK(metadataChanges == 1 && textChanges == 0);
    cursor.clearSelection();
    cursor.insertText(QStringLiteral("."));
    CHECK(textChanges == 1);
    cursor.insertText(QStringLiteral("\nCrumb 2: : new"));
    CHECK(textChanges == 2 && model.sessionCount() == 1);
    int dead = 0;
    {
        QObject temporary;
        model.observe(&temporary, [&](const CrumbChange &) { ++dead; });
    }
    cursor.insertText(QStringLiteral(" words"));
    CHECK(dead == 0 && model.record(1).words == 5);
}

// Compare each incremental edit to indexing the final text from scratch.
// This catches stale rows after undo, a physical-line split, or a bulk paste.
static void compareModel(const CrumbModel &actual, const CrumbModel &expected, int count)
{
    if (expected.neighbor(-1, false) >= 0)
        CHECK(actual.nextHeader() == expected.nextHeader());
    else
        CHECK(actual.neighbor(-1, false) == -1);
    const auto ga = actual.genres(), gb = expected.genres();
    CHECK(ga.size() == gb.size());
    for (int i = 0; i < ga.size(); ++i)
        CHECK(ga[i].key == gb[i].key && ga[i].name == gb[i].name &&
              ga[i].number == gb[i].number && ga[i].warning == gb[i].warning);
    for (int line = 0; line < count; ++line) {
        const auto &a = actual.record(line), &b = expected.record(line);
        CHECK(a.text == b.text && a.valid == b.valid && a.number == b.number && a.title == b.title);
        CHECK(a.bodyStart == b.bodyStart && a.headerStart == b.headerStart && a.words == b.words);
        CHECK(a.headerWarning == b.headerWarning && a.warnings.size() == b.warnings.size());
        for (int i = 0; i < a.warnings.size(); ++i)
            CHECK(a.warnings[i].line == line && a.warnings[i].start == b.warnings[i].start &&
                  a.warnings[i].end == b.warnings[i].end && a.warnings[i].actual == b.warnings[i].actual &&
                  a.warnings[i].expected == b.warnings[i].expected && a.warnings[i].genre == b.warnings[i].genre);
    }
}

static void incrementalIndex()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Book// Crumb 1: A #1: a\nCrumb 2: A #2: b\nCrumb 3: A #3: c"));
    CrumbModel model(&doc);
    const QStringList inserts = { QStringLiteral("x"), QStringLiteral("\n"), QStringLiteral("5"),
        QStringLiteral("C 7: A #2: x\n"), QStringLiteral("\nC 2: B #4: y\nC 4: A #5: z"),
        QStringLiteral("é"), QStringLiteral(" #1"), QStringLiteral(" ") };
    std::mt19937 rng(318);
    for (int step = 0; step < 220; ++step) {
        if (step % 15 == 4 && doc.isUndoAvailable()) {
            doc.undo();
        } else if (step % 15 == 5 && doc.isRedoAvailable()) {
            doc.redo();
        } else {
            const int pos = int(rng() % doc.characterCount());
            const int length = int(rng() % std::min(12, doc.characterCount() - pos));
            replace(doc, pos, length, inserts.at(rng() % inserts.size()));
        }
        QTextDocument fresh;
        fresh.setPlainText(doc.toPlainText());
        CrumbModel reference(&fresh);
        compareModel(model, reference, doc.blockCount());
        std::set<quint64> newNumbers;
        for (int i = 0; i < doc.blockCount(); ++i) {
            const auto &record = reference.record(i);
            if (record.valid && (record.number < 1 || record.number > 3))
                newNumbers.insert(record.number);
        }
        CHECK(model.sessionCount() == int(newNumbers.size()));
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::setlocale(LC_CTYPE, "");
    fieldsAndWords();
    numbersAndSession();
    genreEditsAndWarnings();
    observersAndFormatting();
    incrementalIndex();
    std::puts("Crumb model field, genre, number, session, warning and incremental tests passed.");
    if (argc == 2) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        CHECK(file.open(QIODevice::ReadOnly));
        QTextDocument doc;
        doc.setPlainText(QString::fromUtf8(file.readAll()));
        CrumbModel model(&doc);
        for (int line = 0; line < doc.blockCount(); ++line)
            if (model.record(line).valid)
                std::printf("%d %d\n", line + 1, model.record(line).words);
    }
}
