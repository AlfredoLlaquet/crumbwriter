#include "manuscripteditor.h"
#include "manuscriptlinks.h"
#include "manuscripttext.h"
#include "spellcheck.h"
#include "spellwords.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QThread>
#include <QVBoxLayout>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool valid, const char *message)
{
    if (!valid) { std::fprintf(stderr, "Spelling check failed: %s\n", message); std::abort(); }
}

void until(const std::function<bool()> &done, const char *message)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 6000) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    require(done(), message);
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "write temporary dictionary");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read temporary dictionary");
    return file.readAll();
}

void enter(QWidget *field)
{
    field->setFocus();
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(field, &down);
    QApplication::sendEvent(field, &up);
}

QStringList errors(const SpellWords &words, const QString &input)
{
    QStringList parts;
    for (auto hit : words.issues(input)) parts << input.mid(hit.start, hit.end-hit.start);
    return parts;
}

QStringList underlined(const ManuscriptEditor &ed)
{
    QStringList words;
    for (const auto &selection : ed.extraSelections())
        if (selection.format.underlineStyle() == QTextCharFormat::WaveUnderline)
            words << ManuscriptText::selectedText(selection.cursor);
    return words;
}

void dictionaryAndPhrases()
{
    require(SpellWords::available(), "load American English en_US Hunspell data");
    SpellWords words;
    require(errors(words, QStringLiteral("I’m I'm don’t don't couldn’t didn't color flavor")).isEmpty(),
            "American spellings and typographic contractions");
    require(SpellWords::suggestions("recieve").contains("receive", Qt::CaseInsensitive),
            "real Hunspell corrections");
    words.setExceptions({ "Qzorb Blepik", "Blepik Vryzow", "Sir Xzowan, Jr.", "N’zob" }, {"FROZZPNE"});
    const QString document = QStringLiteral("😀 qzorb blepik VRYZOW, then Qzorb; Sir Xzowan, Jr. and Xzowan. "
                                            "N'zob and frozzpne, FrozZpne.");
    require(errors(words, document) == QStringList{"Qzorb", "Xzowan"},
            "complete overlapping names with literal commas, standalone unknown names, mixed case and apostrophes");
    require(errors(words, "Megaqzorb Blepik and Qzorb Blepikon").contains("Blepik"),
            "no partial word at the start/end of an alias");

    words.setExceptions({QStringLiteral("Qzäß Blepik"), QStringLiteral("Çzorb Àblepik")}, {});
    require(errors(words, QStringLiteral("QZÄSS blepik; C\u0327zorb A\u0300blepik. Blepik.")).size() == 1 &&
            errors(words, QStringLiteral("QZÄSS blepik. Blepik.")) == QStringList{"Blepik"},
            "whole multiword names use full Unicode case folding and canonical spelling at original offsets");

    words.setExceptions({"Qzorb Blepik"}, {"HeDbErG", "O’Zob", "Frozzpne-Vryzow"});
    require(errors(words, QStringLiteral("Hedberg’s HEDBERG'S o'Zob’s Qzorb Blepik’s Frozzpne-Vryzow’s I’m.")).isEmpty(),
            "personal words and complete names accept singular possessives, including mixed-case and curly forms");
    require(errors(words, QStringLiteral("‘Qzorfikk’s’")).value(0) == "Qzorfikk",
            "spelling corrections leave the possessive and the quotation marks intact");
}

void linksInProse()
{
    const QString prose = QStringLiteral("😀 See (https://example.org/A_(B)?k=x,y#z), ‘WWW.xzqy.test/a’. "
                                        "http://[::1]:8100/abc! file://www.example.org "
                                        "x@www.example.org https:///missing.");
    const auto links = ManuscriptLinks::findAll(prose);
    require(links.size() == 3, "only explicit web addresses or separate www. addresses qualify");
    require(prose.sliced(links[0].start, links[0].end - links[0].start) == "https://example.org/A_(B)?k=x,y#z"
            && links[1].target == QUrl("https://www.xzqy.test/a")
            && links[2].target == QUrl("http://[::1]:8100/abc"),
            "UTF-16 bounds, query, fragment, balanced brackets, punctuation, and default HTTPS");
    require(ManuscriptLinks::at(prose, links[0].end, true) == links[0].target
            && ManuscriptLinks::at(prose, links[0].end, false).isEmpty(),
            "a keyboard caret may be on the right edge, a click must hit the address itself");
    SpellWords words;
    const QString text = QStringLiteral("😀 Qzarfnik https://unknownzz.test/Qzarfnik?Qzarfnik=x#Qzarfnik, Qzarfnik");
    require(errors(words, text) == QStringList{"Qzarfnik", "Qzarfnik"},
            "skip complete URLs but check identical words in adjacent prose");
    const auto issues = words.issues(text, text.indexOf("unknownzz") + 2);
    require(issues.size() == 1 && issues.first().start == text.lastIndexOf("Qzarfnik"),
            "spelling begun inside an address still ignores its entire tail");
}

void reviewAndPersistence(const QTemporaryDir &tmp)
{
    QWidget window;
    QVBoxLayout layout(&window);
    ManuscriptEditor ed;
    ed.setCopyDeletedText(true);
    layout.addWidget(&ed);
    window.resize(850, 540);
    SpellCheck spelling(&ed, &window);
    QString lastNotice;
    spelling.onNotice = [&](const QString &notice) { lastNotice = notice; };
    const QString name = tmp.filePath("Crumbs.txt");
    writeFile(name + ".dictionary", QByteArray::fromStdString("ZoRnnix\r\n"));
    const QString original = QStringLiteral(
        "Book//Zzqhdr 1: Story: I’m using ZORNNIX and Qzorb Blepik. Here is Qzorb.\n"
        "Zzqhdr 2: More: recieve this and recieve that. Qzorb twice. See https://qzorb.example/recieve.\n");
    ed.loadText(original);
    QString error;
    require(spelling.openManuscript(name, &error), "open case-insensitive manuscript dictionary");
    spelling.setAliases({"Qzorb Blepik"});
    window.show();
    ed.setFocus();
    const QStringList expected{"Qzorb","recieve","recieve","Qzorb"};
    until([&] { return underlined(ed) == expected; }, "visible spelling ranges skip headers and full aliases");
    QTextCursor prefix(ed.document());
    prefix.setPosition(6, QTextCursor::KeepAnchor);
    prefix.removeSelectedText();
    until([&] { return underlined(ed).count("Zzqhdr") == 2; }, "generic prose checks words even in header-shaped text");
    ed.undo();
    until([&] { return underlined(ed) == expected; }, "undo restores crumb header exemptions");

    QTextEdit::ExtraSelection search;
    search.cursor = QTextCursor(ed.document());
    search.cursor.setPosition(1, QTextCursor::KeepAnchor);
    search.format.setBackground(Qt::yellow);
    ed.setSearchHighlights({search});
    require(ed.extraSelections().size() == expected.size()+1, "search and spelling coexist");
    spelling.setEnabled(false);
    require(underlined(ed).isEmpty() && ed.extraSelections().size()==1, "disabling spelling preserves find");
    spelling.setEnabled(true);
    until([&] { return underlined(ed)==expected; }, "reenable spelling");
    ed.setSearchHighlights({});

    ed.jumpTo(0, false);
    spelling.next();
    auto *review = window.findChild<QDialog *>("spellingReview");
    require(review && review->isVisible(), "modeless review from F8 action");
    auto *issue = review->findChild<QLabel *>("spellingIssue");
    auto *field = review->findChild<QLineEdit *>("spellingReplacement");
    auto button = [&](const char *name) {
        auto *result = review->findChild<QPushButton *>(name);
        require(result != nullptr, "keyboard correction buttons exist");
        return result;
    };
    until([&] { return issue->text().contains("Qzorb"); }, "navigation selects first unknown standalone alias");
    const int originalQ = ed.textCursor().selectionStart();
    require(originalQ == original.indexOf("Here is Qzorb")+8, "exact first spelling position");

    button("spellingIgnore")->click();
    until([&] { return issue->text().contains("recieve"); }, "ignore once selects next error");
    until([&] { return underlined(ed).count("Qzorb")==1; }, "ignore once exempts one occurrence");

    field->setText("receive");
    button("spellingReplaceAll")->click();
    until([&] { return ed.manuscriptText().count("receive")==2 && issue->text().contains("Qzorb"); },
          "replace all occurrences with a single review action");
    require(ed.manuscriptText().contains("https://qzorb.example/recieve."),
            "Replace All in Spelling Review leaves the same spelling in a URL alone");
    require(QApplication::clipboard()->text() == "recieve\nrecieve",
            "bulk spelling correction copies complete displaced spellings as one clipboard entry");
    ed.undo();
    require(ed.manuscriptText()==original, "one Undo restores all spelling replacements");

    ed.jumpTo(original.size()-1, false);
    spelling.next(true);
    until([&] { return issue->text().contains("Qzorb"); }, "reverse navigation opens preceding error");
    button("spellingIgnoreAll")->click();
    until([&] { return issue->text().contains("recieve"); }, "Ignore All advances past other occurrences");
    until([&] { return !underlined(ed).contains("Qzorb"); }, "session ignore all");
    require(readFile(name+".dictionary")=="ZoRnnix\r\n", "ignore operations leave the personal dictionary alone");

    field->setText("receive");
    enter(field);
    until([&] { return ed.manuscriptText().count("recieve")==2 && issue->text().contains("recieve"); },
          "Enter applies one keyboard correction");
    button("spellingAdd")->click();
    until([&] { return readFile(name+".dictionary").contains("recieve\n"); }, "Add to Dictionary writes immediately");
    require(!spelling.hasPending() && readFile(name+".dictionary").startsWith("recieve\n"),
            "UTF-8 without BOM and sorted personal words");
    ed.undo();
    require(ed.manuscriptText()==original, "single correction undo");
    until([&] { return underlined(ed).isEmpty(); }, "new dictionary entries apply immediately");

    const QString copy=tmp.filePath("Second.txt");
    require(spelling.saveAs(copy, &error) && readFile(name+".dictionary")==readFile(copy+".dictionary"),
            "Save As copies exact dictionary bytes");
    spelling.showDictionary();
    auto *manager=window.findChild<QDialog *>("manuscriptDictionary");
    auto *entry=manager->findChild<QLineEdit *>("dictionaryWord");
    auto *list=manager->findChild<QListWidget *>("dictionaryWords");
    require(manager->isVisible() && list->count() == 2, "keyboard-accessible manuscript dictionary list");
    entry->setText(QStringLiteral("L’Zorp"));
    enter(entry);
    require(readFile(copy+".dictionary").contains(QStringLiteral("L’Zorp\n").toUtf8())
            && !readFile(name+".dictionary").contains(QStringLiteral("L’Zorp").toUtf8()), "Save As isolates later edits");
    entry->setText("L'ZORP");
    enter(entry);
    require(list->count()==3, "personal words deduplicated case-insensitively with curly and straight punctuation");

    manager->close();
    review->close();
    require(spelling.openManuscript(copy, &error), "reopen manuscript dictionary from disk");
    until([&] { return underlined(ed)==QStringList{"Qzorb","Qzorb"}; },
          "reopen clears session ignores and preserves learned words");
    QMenu menu;
    ed.spellingMenu(&menu, originalQ);
    require(!menu.actions().isEmpty() && menu.actions().first()->menu(), "context menu offers spelling corrections");
}

void sidecarFailures(const QTemporaryDir &tmp)
{
    QWidget window;
    ManuscriptEditor ed(&window);
    SpellCheck spelling(&ed, &window);
    QString error;
    require(spelling.openManuscript({}, &error), "untitled dictionary starts empty");
    spelling.showDictionary();
    auto *manager = window.findChild<QDialog *>("manuscriptDictionary");
    auto *entry = manager->findChild<QLineEdit *>("dictionaryWord");
    entry->setText("Wugglitz");
    enter(entry);
    require(spelling.hasPending() && !spelling.flush(&error), "untitled word waits for Save As");
    const auto path = tmp.filePath("conflict.txt");
    writeFile(path+".dictionary", "existing\n");
    require(!spelling.saveAs(path, &error) && spelling.hasPending() && readFile(path+".dictionary")=="existing\n",
            "conflicting target preserved with pending dictionary");
    const auto rescue = tmp.filePath("rescue.txt");
    require(spelling.saveAs(rescue,&error) && !spelling.hasPending() && readFile(rescue+".dictionary")=="Wugglitz\n",
            "pending dictionary can be saved under a fresh name");
    const auto invalid = tmp.filePath("invalid.txt");
    writeFile(invalid+".dictionary", QByteArray("broken\xff",7));
    require(!spelling.openManuscript(invalid, &error), "report malformed dictionary");
    entry->setText("another");
    enter(entry);
    require(readFile(invalid+".dictionary")==QByteArray("broken\xff",7), "preserve malformed bytes on failed add");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary manuscripts");
    dictionaryAndPhrases();
    linksInProse();
    reviewAndPersistence(tmp);
    sidecarFailures(tmp);
    std::puts("American English spelling, keyboard review, alias exceptions, underlines and manuscript dictionaries passed.");
}
