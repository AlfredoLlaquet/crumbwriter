#include "crumbmodel.h"
#include "manuscripttext.h"

#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>

#include <clocale>
#include <cstdio>
#include <random>

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
    CHECK(model.crumbCount() == 1 && model.totalWords() == 5 && model.record(0).words == 4);
    doc.clear();
    CHECK(model.crumbCount() == 0 && model.totalWords() == 0);
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

int main(int argc, char **argv)
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    qputenv("XDG_DATA_HOME", temp.path().toLocal8Bit());
    QApplication app(argc, argv);
    std::setlocale(LC_CTYPE, "");
    fieldsAndWords();
    numbersAndDailyCount();
    manuscriptStatistics();
    dailyPersistence(temp.path());
    genreEditsAndWarnings();
    observersAndFormatting();
    incrementalIndex();
    std::puts("Crumb model fields, genres, numbering, daily counts and manuscript statistics passed.");
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
