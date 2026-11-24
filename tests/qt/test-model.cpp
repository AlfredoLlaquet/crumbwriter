#include "crumbmodel.h"
#include "manuscripttext.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

static void check(bool passed, const char *function, int line, const char *condition)
{
    if (!passed) {
        std::fprintf(stderr, "model:%s:%d: %s\n", function, line, condition);
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(condition) check(bool(condition), __func__, __LINE__, #condition)

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
    CHECK(model.todayCount() == 0);
}

static void numbersAndDailyCount()
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
    CHECK(model.todayCount() == 2 && model.nextHeader() == u"Pip 9: : ");
    doc.undo();
    CHECK(model.todayCount() == 0 && model.nextHeader() == u"Entry 8: : ");
    doc.redo();
    CHECK(model.todayCount() == 2);
    model.markOpened();
    CHECK(model.todayCount() == 0);
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
    CHECK(textChanges == 2 && model.todayCount() == 1);
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
    CHECK(actual.totalWords() == expected.totalWords());
    CHECK(actual.crumbCount() == expected.crumbCount());
    CHECK(actual.isGeneric() == expected.isGeneric());
    CHECK(actual.paragraphCount() == expected.paragraphCount());
    CHECK(actual.titleLines() == expected.titleLines());
    if (expected.crumbCount())
        CHECK(actual.nextHeader() == expected.nextHeader());
    CHECK(actual.neighbor(-1, false) == expected.neighbor(-1, false));
    const auto ga = actual.genres(), gb = expected.genres();
    CHECK(ga.size() == gb.size());
    for (int i = 0; i < ga.size(); ++i)
        CHECK(ga[i].key == gb[i].key && ga[i].name == gb[i].name &&
              ga[i].number == gb[i].number && ga[i].warning == gb[i].warning);
    for (int line = 0; line < count; ++line) {
        const auto &a = actual.record(line), &b = expected.record(line);
        CHECK(actual.paragraphRole(line) == expected.paragraphRole(line));
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
        QStringLiteral("é"), QStringLiteral(" #1"), QStringLiteral(" "), QStringLiteral("//"),
        QStringLiteral(".\n\n"), QStringLiteral("\n\nChapter\n\n"), QStringLiteral("\nTitle\n-\n"), QStringLiteral("-") };
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
        CHECK(model.todayCount() >= std::max(0, model.crumbCount() - 3));
        CHECK(model.todayCount() <= model.crumbCount());
    }
}

static void manuscriptStatistics()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Work title//Crumb 1: T: a—b / c / / d\n"
                                    "unfinished extra\n\nCrumb 1: : z\n"));
    CrumbModel model(&doc);
    CHECK(model.crumbCount() == 2);
    CHECK(model.totalWords() == 16); // 10 words on line 1, 2 on line 2, 4 on line 4.
    CHECK(model.record(0).words == 8);
    replace(doc, doc.characterCount()-1, 0, QStringLiteral("trailing text"));
    CHECK(model.totalWords() == 18 && model.crumbCount() == 2);
    doc.undo();
    CHECK(model.totalWords() == 16);
    doc.setPlainText(QStringLiteral("Crumb 1: URL: https://site.tld//tail\n"));
    CHECK(model.isGeneric() && model.totalWords() == 4 && model.record(0).words == 4);
    doc.clear();
    CHECK(model.crumbCount() == 0 && model.totalWords() == 0);
}

static void paragraphStructure()
{
    using Role = ParagraphRole;
    QTextDocument doc;
    const QString opening = QStringLiteral("A first sentence.\nAnother sentence.\n\nA chapter.\n-\nBody text.\n");
    doc.setPlainText(opening);
    CrumbModel model(&doc);
    CHECK(model.isGeneric() && model.paragraphCount() == 4 && model.titleLines() == QVector<int>{3});
    CHECK(model.paragraphRole(0) == Role::Body && model.paragraphRole(2) == Role::Blank);
    CHECK(model.paragraphRole(3) == Role::ChapterTitle && model.paragraphRole(5) == Role::Body);
    CHECK(model.neighbor(1, false) == 2 && model.neighbor(3, true) == 2);
    CHECK(model.neighbor(5, false) == -1 && model.neighbor(6, true) == 5);
    CrumbChange last;
    model.observe(&doc, [&](const CrumbChange &change) { last = change; });
    replace(doc, doc.findBlockByNumber(5).position(), 0, QStringLiteral("More "));
    CHECK(!last.titlesChanged && !last.modeChanged);
    replace(doc, doc.findBlockByNumber(3).position(), 1, QStringLiteral("The"));
    CHECK(last.titlesChanged && model.titleLines() == QVector<int>{3});
    replace(doc, doc.findBlockByNumber(4).position(), 0, QStringLiteral("Prose."));
    CHECK(last.titlesChanged && last.firstLine <= 3 && model.titleLines().isEmpty());
    doc.undo();
    CHECK(model.paragraphRole(3) == Role::ChapterTitle);
    replace(doc, doc.findBlockByNumber(1).position(), 0, QStringLiteral("\n"));
    const QVector<int> moved{0, 4};
    CHECK(last.titlesChanged && model.titleLines() == moved);
    CHECK(model.paragraphRole(0) == Role::DocumentTitle);
    CHECK(model.neighborTitle(0, false) == 4 && model.neighborTitle(4, true) == 0);
    CHECK(model.neighborTitle(0, true) == -1 && model.neighborTitle(4, false) == -1);

    doc.setPlainText(QStringLiteral("ÁRBOL 2.\nProse."));
    CHECK(model.paragraphRole(0) == Role::DocumentTitle);
    doc.setPlainText(QStringLiteral("1912.\nProse."));
    CHECK(model.paragraphRole(0) == Role::Body);
    doc.setPlainText(QStringLiteral("Mixed Case\nProse."));
    CHECK(model.paragraphRole(0) == Role::DocumentTitle);
    doc.setPlainText(QStringLiteral("\nName of chapter\n  \t\nText."));
    CHECK(model.paragraphRole(0) == Role::Blank && model.paragraphRole(1) == Role::Body);
    CHECK(model.paragraphCount() == 2);
    doc.setPlainText(QStringLiteral("First paragraph.\n"));
    CHECK(model.titleLines().isEmpty() && model.paragraphCount() == 1);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral("\n"));
    CHECK(model.paragraphRole(0) == Role::DocumentTitle && model.paragraphCount() == 1);
    doc.undo();
    CHECK(model.paragraphRole(0) == Role::Body && model.titleLines().isEmpty());
    doc.setPlainText(QStringLiteral("Body.\n\nChapter"));
    CHECK(model.titleLines().contains(0) && !model.titleLines().contains(2));
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral("\n\n"));
    CHECK(model.paragraphRole(2) == Role::Body); // Blank lines preserve prose.
    doc.setPlainText(QStringLiteral("First paragraph.\n-\nBody\n----\n---\n\n"));
    CHECK(model.paragraphRole(0) == Role::DocumentTitle && model.paragraphRole(1) == Role::TitleMarker);
    CHECK(model.paragraphRole(2) == Role::ChapterTitle && model.paragraphRole(3) == Role::TitleMarker);
    CHECK(model.paragraphRole(4) == Role::Body); // An unattached run stays literal.
    CHECK(model.paragraphCount() == 3 && model.totalWords() == 4 && model.record(1).words == 0);
    model.markOpened();
    replace(doc, doc.findBlockByNumber(1).position(), 0, QStringLiteral("-"));
    CHECK(model.todayWords() == 0 && model.totalWords() == 4);
    replace(doc, doc.findBlockByNumber(3).position(), 0, QStringLiteral("x"));
    CHECK(model.paragraphRole(2) == Role::Body && model.paragraphRole(4) == Role::TitleMarker);
    CHECK(model.documentEndPosition() == doc.findBlockByNumber(3).position() + 5);
    doc.undo();
    CHECK(model.paragraphRole(3) == Role::TitleMarker && model.todayWords() == 0);
    doc.setPlainText(QStringLiteral("Prose.\nA title\n-\n\n"));
    CHECK(model.documentEndPosition() == doc.findBlockByNumber(2).position() - 1);
    doc.setPlainText(QStringLiteral("General title\n\n\nA chapter\n -\nOther\n- more"));
    CHECK(model.paragraphRole(1) == Role::TitleSpacer && model.paragraphRole(2) == Role::Blank);
    CHECK(model.titleLines() == QVector<int>{0});
    doc.setPlainText(QStringLiteral("\n\n"));
    CHECK(model.documentEndPosition() == 0);
}

static void firstLineMode()
{
    QTextDocument doc;
    const QString first = QStringLiteral("Guide to https://example.org/a//b and www.example.com/x//y");
    doc.setPlainText(first + QStringLiteral("\n//Crumb 2: Old form: Text."));
    CrumbModel model(&doc);
    CHECK(model.isGeneric() && model.record(0).words == 5);
    CrumbChange last;
    model.observe(&doc, [&](const CrumbChange &change) { last = change; });
    const QString title = first + QStringLiteral(" 📘 Edition");
    const QString header = QStringLiteral("//Cranolo 1: The Inspector: A goose audits the moon for bumps.");
    replace(doc, first.size(), 0, title.mid(first.size()) + header);
    CHECK(!model.isGeneric() && last.modeChanged && last.titlesChanged && model.titleLines().isEmpty());
    const auto &row = model.record(0);
    CHECK(row.valid && row.label == u"Cranolo" && row.title == u"The Inspector");
    CHECK(row.recordStart == title.size() + 2 && row.headerStart == row.recordStart);
    CHECK(row.text.mid(row.bodyStart) == u"A goose audits the moon for bumps.");
    CHECK(row.words == 11 && model.totalWords() > row.words);
    doc.undo();
    CHECK(model.isGeneric() && last.modeChanged);
    doc.redo();
    CHECK(!model.isGeneric() && last.modeChanged);
    replace(doc, title.size() + 2, header.size() - 2, {});
    CHECK(!model.isGeneric() && !model.record(0).valid); // A work title can precede its first header.
    doc.setPlainText(QStringLiteral("Title\nC 2: : //"));
    CHECK(model.isGeneric());
    doc.setPlainText(QStringLiteral("C 1: : example.org/a//b W W"));
    CHECK(model.isGeneric());
    doc.setPlainText(QStringLiteral("Notes (<https://example.org/a//b>)//C 1: A: x"));
    CHECK(!model.isGeneric() && model.record(0).label == u"C");
}

static void dailyPersistence(const QString &directory)
{
    const QString path = directory + "/Manuscript.txt";
    const QString copy = directory + "/Copy.txt";
    QDate date(2026, 11, 20);
    const auto clock = [&] { return date; };
    const QString old = QStringLiteral("Crumb 1: A: a\nCrumb 2: B: b");
    const QString three = QStringLiteral("\nCrumb 3: C: c\nCrumb 4: D: d\nCrumb 5: E: e");
    auto reopen = [&](const QString &name, const QString &text) {
        QTextDocument file;
        file.setPlainText(text);
        CrumbModel model(&file, clock);
        CHECK(model.openDailyCount(name).isEmpty());
        return model.todayCount();
    };
    QTextDocument doc;
    doc.setPlainText(old);
    CrumbModel model(&doc, clock);
    CHECK(model.openDailyCount(path).isEmpty() && model.todayCount() == 0);
    replace(doc, doc.characterCount() - 1, 0, three);
    CHECK(model.todayCount() == 3 && reopen(path, doc.toPlainText()) == 3);
    const int last = doc.lastBlock().position();
    replace(doc, last - 1, doc.characterCount() - last, {});
    CHECK(model.todayCount() == 2 && reopen(path, doc.toPlainText()) == 2);
    CHECK(reopen(path, old + three) == 3); // A pending deletion discarded before saving.
    doc.undo();
    CHECK(model.todayCount() == 3);
    replace(doc, 0, doc.findBlockByNumber(1).position(), {});
    CHECK(model.todayCount() == 3 && reopen(path, doc.toPlainText()) == 3);
    doc.undo();
    CHECK(model.todayCount() == 3);

    // Correct an old header even when the digits disappear between keypresses.
    replace(doc, 6, 1, {});
    replace(doc, 6, 0, QStringLiteral("42"));
    CHECK(model.todayCount() == 3 && reopen(path, doc.toPlainText()) == 3);
    CHECK(reopen(path, old) == 0); // Published text before this correction.
    // The displaced header number can be reused for a genuine new crumb.
    // Reopening then deleting that new record must reduce the counter.
    {
        QTextDocument reused;
        reused.setPlainText(doc.toPlainText() + QStringLiteral("\nCrumb 1: Another: one"));
        CrumbModel restored(&reused, clock);
        CHECK(restored.openDailyCount(path).isEmpty() && restored.todayCount() == 4);
        const int end = reused.lastBlock().position() - 1;
        replace(reused, end, reused.characterCount() - 1 - end, {});
        CHECK(restored.todayCount() == 3);
    }
    CHECK(model.saveDailyCountAs(copy).isEmpty() && reopen(copy, doc.toPlainText()) == 3);
    CHECK(reopen(directory + "/Independent.txt", doc.toPlainText()) == 0);
    date = date.addDays(1);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral("\nCrumb 6: F: f"));
    CHECK(model.todayCount() == 1 && reopen(copy, doc.toPlainText()) == 1);
    CHECK(reopen(path, old + three) == 0);
    date = date.addDays(1);
    int changes = 0;
    model.observe(&doc, [&](const CrumbChange &change) { if (!change.textChanged) ++changes; });
    model.refreshDay(); // Also the idle-midnight timer entry point.
    CHECK(changes == 1 && model.todayCount() == 0);

    // The saved file can precede two chained renumberings. Reordering its
    // lines must not cause both old identities to be assigned to one number.
    QTextDocument renumbered;
    renumbered.setPlainText(old);
    CrumbModel ids(&renumbered, clock);
    const QString renamed = directory + "/Renumbered.txt";
    CHECK(ids.openDailyCount(renamed).isEmpty());
    replace(renumbered, 6, 1, QStringLiteral("9"));
    replace(renumbered, renumbered.findBlockByNumber(1).position() + 6, 1, QStringLiteral("1"));
    CHECK(ids.todayCount() == 0);
    CHECK(reopen(renamed, QStringLiteral("Crumb 1: B: b\nCrumb 9: A: a")) == 0);
    CHECK(reopen(renamed, old) == 0);

    const QString identical = QStringLiteral("Crumb 1: Same: text");
    for (bool insertAbove : {false, true}) {
        QTextDocument duplicates;
        duplicates.setPlainText(identical);
        CrumbModel daily(&duplicates, clock);
        if (insertAbove) replace(duplicates, 0, 0, identical + u'\n');
        else replace(duplicates, identical.size(), 0, u'\n' + identical);
        CHECK(daily.todayCount() == 1);
        // Delete the old copy. The newly written copy still counts.
        replace(duplicates, insertAbove ? identical.size() : 0, identical.size() + 1, {});
        CHECK(duplicates.toPlainText() == identical && daily.todayCount() == 1);
        duplicates.undo();
        CHECK(daily.todayCount() == 1);
        replace(duplicates, insertAbove ? 0 : identical.size(), identical.size() + 1, {});
        CHECK(daily.todayCount() == 0);
    }

    // The old and new file contents are byte-for-byte identical. A late
    // worker result must publish the daily identities of its own revision.
    QTextDocument same;
    same.setPlainText(identical);
    CrumbModel ledger(&same, clock);
    const QString source = directory + "/Duplicates.txt";
    const QString target = directory + "/DuplicatesCopy.txt";
    const QString draft = directory + "/DuplicatesDraft.txt";
    CHECK(ledger.openDailyCount(source).isEmpty());
    const auto morning = ledger.dailySnapshot();
    replace(same, identical.size(), 0, u'\n' + identical);
    replace(same, 0, identical.size() + 1, {});
    CHECK(ledger.todayCount() == 1 && reopen(source, identical) == 0);
    const auto installed = ledger.dailySnapshot();
    CHECK(morning != installed);
    replace(same, identical.size(), 0, QStringLiteral("!"));
    CHECK(ledger.dailySnapshot() == installed); // Body typing reuses the cached number layout.
    replace(same, 0, same.characterCount() - 1, {});
    CHECK(ledger.todayCount() == 0);
    CHECK(ledger.publishDailySnapshot(source, installed).isEmpty());
    CHECK(reopen(source, identical) == 1);
    CHECK(ledger.publishDailySnapshot(draft, morning).isEmpty());
    CHECK(reopen(draft, identical) == 0 && reopen(source, identical) == 1);
    CHECK(ledger.saveDailyCountAs(target).isEmpty());
    CHECK(ledger.publishDailySnapshot(target, morning).isEmpty());
    CHECK(reopen(target, identical) == 0 && reopen(source, identical) == 1);
    CHECK(ledger.publishDailySnapshot(draft, installed).isEmpty());
    CHECK(reopen(draft, identical) == 1 && reopen(target, identical) == 0);
    date = date.addDays(1);
    CHECK(ledger.publishDailySnapshot(target, installed).isEmpty());
    CHECK(reopen(target, identical) == 0);

    // A pre-midnight unsaved deletion leaves a different set of old rows on
    // disk. Closing with Discard the next day must restore both as older.
    QTextDocument night;
    night.setPlainText(old);
    CrumbModel late(&night, clock);
    const QString overnight = directory + "/Overnight.txt";
    CHECK(late.openDailyCount(overnight).isEmpty());
    replace(night, 0, night.findBlockByNumber(1).position(), {});
    date = date.addDays(1);
    late.refreshDay();
    CHECK(reopen(overnight, old) == 0);
    CHECK(reopen(overnight, old + QStringLiteral("\nCrumb 3: Today: text")) == 1);
}

static void dailyWordPersistence(const QString &directory)
{
    const QString source = directory + "/Novel.txt";
    const QString copy = directory + "/Novel copy.txt";
    const QString draft = directory + "/unsaved draft";
    QDate date(2026, 11, 20);
    const auto clock = [&] { return date; };
    const auto reopen = [&](const QString &name, const QString &text) {
        QTextDocument opened;
        opened.setPlainText(text);
        CrumbModel model(&opened, clock);
        CHECK(model.openDailyCount(name).isEmpty());
        return model.todayWords();
    };
    QTextDocument doc;
    const QString old = QStringLiteral("A novel\none two three.");
    doc.setPlainText(old);
    CrumbModel model(&doc, clock);
    CHECK(model.openDailyCount(source).isEmpty() && model.todayWords() == 0);
    replace(doc, 0, doc.findBlockByNumber(1).position(), {});
    CHECK(model.todayWords() == -2);
    const auto fewer = model.dailySnapshot();
    CHECK(model.publishDailySnapshot(draft, fewer).isEmpty());
    CHECK(reopen(draft, doc.toPlainText()) == -2);
    CHECK(reopen(source, old) == 0); // Saving a draft does not change the manuscript's revision.
    doc.undo();
    CHECK(model.todayWords() == 0);
    doc.redo();
    CHECK(model.todayWords() == -2);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral(" Geese write—more / words."));
    CHECK(model.todayWords() == 2 && model.totalWords() == 7);
    const QString savedText = doc.toPlainText();
    const auto saved = model.dailySnapshot();
    CHECK(model.publishDailySnapshot(source, saved).isEmpty());
    CHECK(reopen(source, savedText) == 2);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral(" Later."));
    CHECK(model.todayWords() == 3);
    CHECK(model.saveDailyCountAs(copy).isEmpty());
    CHECK(model.publishDailySnapshot(copy, model.dailySnapshot()).isEmpty());
    CHECK(reopen(copy, doc.toPlainText()) == 3 && reopen(source, savedText) == 2);
    replace(doc, 0, 0, QStringLiteral("Book//C 1: T: "));
    CHECK(!model.isGeneric());
    doc.undo();
    CHECK(model.isGeneric() && model.todayWords() == 3);

    date = date.addDays(1);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral(" Today starts."));
    CHECK(model.todayWords() == 2);
    CHECK(model.publishDailySnapshot(draft, saved).isEmpty()); // Worker completed across midnight.
    CHECK(reopen(draft, savedText) == 0);
    CHECK(model.publishDailySnapshot(copy, model.dailySnapshot()).isEmpty());
    CHECK(reopen(copy, doc.toPlainText()) == 2);

    const QString onDisk = doc.toPlainText();
    replace(doc, 0, doc.characterCount() - 1, QStringLiteral("one"));
    date = date.addDays(1);
    model.refreshDay();
    CHECK(model.todayWords() == 0 && reopen(copy, onDisk) == 0);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral(" two"));
    CHECK(model.todayWords() == 1 && model.publishDailySnapshot(copy, model.dailySnapshot()).isEmpty());
    CHECK(reopen(copy, doc.toPlainText()) == 1);
    CHECK(reopen(directory + "/Another novel.txt", onDisk) == 0);

    // Version 2.2 stored crumb identities without a word baseline.
    const QString legacy = directory + "/Legacy.txt";
    QFile manuscript(legacy);
    CHECK(manuscript.open(QIODevice::WriteOnly));
    const QByteArray oldText("Work//C 1: A: x\nC 2: B: y");
    CHECK(manuscript.write(oldText) == oldText.size());
    manuscript.close();
    // Match 2.2's key, including expansion of short Windows temp paths.
    const auto name = QFileInfo(legacy).canonicalFilePath();
    CHECK(!name.isEmpty());
    const auto key = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QFile state(data + "/crumbwriter/daily/" + QString::fromLatin1(key) + ".json");
    CHECK(state.open(QIODevice::WriteOnly));
    const auto oldState = QByteArray("{\"version\":1,\"headers\":[\"1\"],\"renamed\":[],\"date\":\"") +
                          date.toString(Qt::ISODate).toLatin1() + "\"}";
    CHECK(state.write(oldState) == oldState.size());
    state.close();
    QTextDocument migrated;
    migrated.setPlainText(QString::fromUtf8(oldText));
    CrumbModel counted(&migrated, clock);
    CHECK(counted.openDailyCount(legacy).isEmpty());
    CHECK(counted.todayCount() == 1 && counted.todayWords() == 0);
    replace(migrated, migrated.characterCount() - 1, 0, QStringLiteral(" later"));
    CHECK(counted.todayWords() == 1);
}

static void dailyHeaderEndings(const QString &directory)
{
    const QString path = directory + "/Terminal rows.txt";
    const QString identical = QStringLiteral("Book//Crumb 1: Same: Text.");
    const QString duplicate = QStringLiteral("Crumb 1: Same: Text.");
    QTextDocument doc;
    doc.setPlainText(identical);
    CrumbModel model(&doc);
    CHECK(model.openDailyCount(path).isEmpty());
    replace(doc, identical.size(), 0, u'\n' + duplicate);
    replace(doc, 0, identical.size() + 1, {}); // Only the new copy of header 1 survives.
    replace(doc, duplicate.size(), 0, QStringLiteral("\n\n\n"));
    CHECK(model.todayCount() == 1);
    const QByteArray checkpoint = model.dailySnapshot();
    CHECK(model.publishDailySnapshot(path, checkpoint).isEmpty());
    const auto reopen = [&](const QString &text, int removedLines) {
        QTextDocument opened;
        opened.setPlainText(text);
        CrumbModel restored(&opened);
        restored.markOpened(removedLines);
        CHECK(restored.openDailyCount(path).isEmpty());
        CHECK(restored.todayCount() == 1);
    };
    reopen(duplicate + u'\n', 0);
    reopen(duplicate, 0);

    // 2.5 fingerprints every physical Qt block, including all terminal zero
    // header slots. The loader trimmed two of the original three empty rows.
    auto legacy = QJsonDocument::fromJson(checkpoint).object();
    legacy.remove("shapeFormat");
    QByteArray numbers(4 * sizeof(quint64), '\0');
    numbers[7] = 1; // Header number one in big-endian order, followed by three zeros.
    legacy.insert("shape", QString::fromLatin1(QCryptographicHash::hash(numbers, QCryptographicHash::Sha256).toHex()));
    CHECK(model.publishDailySnapshot(path, QJsonDocument(legacy).toJson(QJsonDocument::Compact)).isEmpty());
    reopen(duplicate + u'\n', 2);
}

static void adjustedCounts(const QString &directory)
{
    QDate day(2026, 11, 20);
    const auto clock = [&] { return day; };
    const QString manuscript = directory + "/Adjusted.txt";
    const QString morning = QStringLiteral("Book//Crumb 1: Beginning: The door opened.");
    QTextDocument doc;
    doc.setPlainText(morning);
    CrumbModel model(&doc, clock);
    CHECK(model.openDailyCount(manuscript).isEmpty());
    const auto reopen = [&](const QString &path, const QString &contents, qint64 crumbs, qint64 words) {
        QTextDocument file;
        file.setPlainText(contents);
        CrumbModel restored(&file, clock);
        CHECK(restored.openDailyCount(path).isEmpty());
        if (restored.todayCount() != crumbs || restored.todayWords() != words)
            qWarning() << "Reopening" << path << "on" << day << "expected crumbs/words" << crumbs << words
                       << "and restored" << restored.todayCount() << restored.todayWords();
        CHECK(restored.todayCount() == crumbs && restored.todayWords() == words);
    };
    doc.setModified(false);
    CHECK(model.setTodayCount(17).isEmpty() && model.todayCount() == 17 && !doc.isModified());
    reopen(manuscript, morning, 17, 0);
    const QString second = QStringLiteral("\nCrumb 2: Afterward: The door closed.");
    replace(doc, morning.size(), 0, second);
    CHECK(model.todayCount() == 18);
    const auto pending = model.dailySnapshot();
    CHECK(model.setTodayCount(5).isEmpty());
    const qint64 wordsAdded = model.todayWords();
    CHECK(model.publishDailySnapshot(manuscript, pending).isEmpty()); // Save began before the adjustment.
    reopen(manuscript, doc.toPlainText(), 5, wordsAdded);
    doc.undo();
    CHECK(model.todayCount() == 4);
    doc.redo();
    CHECK(model.todayCount() == 5);
    CHECK(model.setTodayCount(0).isEmpty());
    doc.undo();
    CHECK(model.todayCount() == 0);
    doc.redo();
    CHECK(model.todayCount() == 0);

    replace(doc, 0, 6, {}); // Switch to prose in the same manuscript.
    CHECK(model.isGeneric() && model.setTodayCount(-12).isEmpty());
    CHECK(model.todayWords() == -12 && model.todayCount() == 0);
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral(" Again."));
    CHECK(model.todayWords() == -11);
    doc.undo();
    CHECK(model.todayWords() == -12);
    CHECK(model.publishDailySnapshot(manuscript, model.dailySnapshot()).isEmpty());
    reopen(manuscript, doc.toPlainText(), 0, -12);
    reopen(directory + "/Unadjusted.txt", doc.toPlainText(), 0, 0);
    doc.undo();
    CHECK(!model.isGeneric() && model.todayCount() == 0);
    CHECK(model.setTodayCount(7).isEmpty());
    doc.redo();
    CHECK(model.isGeneric() && model.todayWords() == -12 && model.todayCount() == 7);
    CHECK(model.setTodayCount(std::numeric_limits<qint64>::max()).isEmpty());
    replace(doc, doc.characterCount() - 1, 0, QStringLiteral(" Again."));
    CHECK(model.todayWords() == std::numeric_limits<qint64>::max());
    doc.undo();
    reopen(manuscript, doc.toPlainText(), 7, std::numeric_limits<qint64>::max());
    day = day.addDays(1);
    model.refreshDay();
    CHECK(model.todayWords() == 0 && model.todayCount() == 0);
    reopen(manuscript, doc.toPlainText(), 0, 0);

    QTextDocument unnamed;
    unnamed.setPlainText(QStringLiteral("Another morning."));
    CrumbModel draft(&unnamed, clock);
    const QString recovery = directory + "/Untitled recovery draft";
    CHECK(draft.publishDailySnapshot(recovery, draft.dailySnapshot()).isEmpty());
    CHECK(draft.setTodayCount(42).isEmpty());
    reopen(recovery, unnamed.toPlainText(), 0, 42);
    day = day.addDays(1);
    draft.refreshDay();
    CHECK(draft.setTodayCount(-7).isEmpty());
    reopen(recovery, unnamed.toPlainText(), 0, -7);
}

int main(int argc, char **argv)
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    qputenv("XDG_DATA_HOME", temp.path().toLocal8Bit());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QStandardPaths::setTestModeEnabled(true);
#endif
    QApplication app(argc, argv);
    std::setlocale(LC_CTYPE, "");
    fieldsAndWords();
    numbersAndDailyCount();
    manuscriptStatistics();
    dailyPersistence(temp.path());
    adjustedCounts(temp.path());
    dailyHeaderEndings(temp.path());
    paragraphStructure();
    firstLineMode();
    dailyWordPersistence(temp.path());
    genreEditsAndWarnings();
    observersAndFormatting();
    incrementalIndex();
    std::puts("Manuscript modes, paragraph headings, crumb fields, daily counts and statistics passed.");
    if (argc == 2 || (argc == 3 && QByteArray(argv[1]) == "--stats")) {
        QFile file(QString::fromLocal8Bit(argv[argc-1]));
        CHECK(file.open(QIODevice::ReadOnly));
        QTextDocument doc;
        ManuscriptText::load(&doc, QString::fromUtf8(file.readAll()));
        CrumbModel model(&doc);
        if (argc == 3) {
            std::printf("%d crumbs, %lld words\n", model.crumbCount(), static_cast<long long>(model.totalWords()));
            return 0;
        }
        for (int line = 0; line < doc.blockCount(); ++line)
            if (model.record(line).valid)
                std::printf("%d %d\n", line + 1, model.record(line).words);
    }
}
