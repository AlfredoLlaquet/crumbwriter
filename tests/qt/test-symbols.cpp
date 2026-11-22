#include "manuscripteditor.h"
#include "symbolcatalog.h"
#include "symbolpicker.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) qFatal("Symbol picker: %s", message);
}

bool until(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 2000) {
        QApplication::processEvents();
        QThread::msleep(2);
    }
    return ready();
}

void key(QWidget *target, Qt::Key code, Qt::KeyboardModifiers mods = {}, const QString &text = {})
{
    QKeyEvent down(QEvent::KeyPress, code, mods, text);
    QApplication::sendEvent(target, &down);
    QKeyEvent up(QEvent::KeyRelease, code, mods, text);
    QApplication::sendEvent(target, &up);
    QApplication::processEvents();
}

void catalogSearch()
{
    const auto &catalog = SymbolCatalog::instance();
    check(catalog.themes().size() >= 12 && catalog.search({}).size() >= 500, "curated resource is present");
    auto first = [&](const QString &query) {
        auto rows = catalog.search(query);
        return rows.isEmpty() ? QString() : catalog.at(rows.first()).text;
    };
    check(first("HOLLOW heart") == QStringLiteral("♡") && first("->") == QStringLiteral("→") &&
          first("1/2") == QStringLiteral("½"), "plain names and conventional notation");
    check(first("Musical Symbol G Clef") == QStringLiteral("𝄞") &&
          first("U+1D11E") == QStringLiteral("𝄞"), "official name and supplementary code point");
    check(first("0x2014") == QStringLiteral("—") && first("2014") == QStringLiteral("—") &&
          first(QStringLiteral("—")) == QStringLiteral("—"), "hexadecimal formats and pasted symbol");
    check(first("fávorite star") == QStringLiteral("★"), "accent-insensitive mixed keyword query");
    const QString nbsp(QChar(0x00a0));
    check(catalog.search(nbsp).size() == 1 && first(nbsp) == nbsp, "pasted invisible symbol");
    check(!catalog.search("U+26").isEmpty() && catalog.search("U+D800").isEmpty() &&
          catalog.search("U+FEFF").isEmpty() && catalog.search("U+0416").isEmpty(),
          "hex prefix searches stay within the curated catalog");
    const QStringList scoped{"U+2661", "U+2014"};
    const auto favorites = catalog.search({}, {}, &scoped);
    check(favorites.size() == 2 && catalog.at(favorites.first()).code == scoped.first(), "saved order");
    check(catalog.search("moon", "Punctuation").isEmpty() && !catalog.search("lunar", "Sky and weather").isEmpty(),
          "combine a theme and a description");
}

void picker(QSettings &settings)
{
    QWidget owner;
    ManuscriptEditor editor(&owner);
    editor.loadText("One goose");
    editor.jumpTo(9);
    owner.show();
    SymbolPicker dialog(&editor, settings, &owner);
    dialog.openPicker();
    QApplication::processEvents();
    auto *search = dialog.findChild<QLineEdit *>("symbolSearch");
    auto *themes = dialog.findChild<QComboBox *>("symbolTheme");
    auto *list = dialog.findChild<QListView *>("symbolGrid");
    auto *details = dialog.findChild<QLabel *>("symbolDetails");
    auto *insert = dialog.findChild<QPushButton *>("symbolInsert");
    check(search && themes && list && details && insert && !dialog.isModal() && until([&] { return search->hasFocus(); }),
          "nonmodal picker opens at the search field");

    key(search, Qt::Key_T, Qt::AltModifier);
    check(until([&] { return themes->view()->isVisible(); }), "Alt+T opens the theme selector");
    themes->hidePopup();
    key(themes, Qt::Key_S, Qt::AltModifier);
    check(until([&] { return search->hasFocus(); }), "Alt+S returns to search");

    search->setText("U+1D11E");
    check(list->model()->rowCount() == 1 && details->text().contains("MUSICAL SYMBOL G CLEF"),
          "typing selects the first result and shows its metadata");
    key(search, Qt::Key_D, Qt::ControlModifier);
    themes->setCurrentIndex(themes->findText("Favorites"));
    check(list->model()->rowCount() == 1 && list->currentIndex().data(Qt::UserRole) == QStringLiteral("𝄞"),
          "keyboard favorite appears in its own collection");

    QApplication::clipboard()->setText("clipboard sentinel");
    key(search, Qt::Key_Return, Qt::ControlModifier);
    check(dialog.isVisible() && editor.manuscriptText().toUtf8() == QByteArray("One goose\xf0\x9d\x84\x9e") &&
          QApplication::clipboard()->text() == "clipboard sentinel", "keep-open inserts exact UTF-8 without copying");
    key(search, Qt::Key_Return);
    check(!dialog.isVisible() && until([&] { return editor.hasFocus(); }) && editor.manuscriptText() == QStringLiteral("One goose𝄞𝄞"),
          "Enter returns to the manuscript");
    editor.undo();
    check(editor.manuscriptText() == QStringLiteral("One goose𝄞"), "each insertion is one undo step");
    editor.undo();
    check(editor.manuscriptText() == QStringLiteral("One goose"), "second undo restores the initial manuscript");

    editor.selectRange(4, 9);
    dialog.openPicker();
    search->setText("U+00A0");
    check(list->currentIndex().data().toString() != QString(QChar(0x00a0)) &&
          details->text().contains("two bars"), "width markers explain the invisible symbol");
    key(search, Qt::Key_Return);
    check(editor.manuscriptText().toUtf8() == QByteArray("One \xc2\xa0"), "space symbol replaces a manuscript selection");
    editor.undo();
    check(editor.manuscriptText() == "One goose", "selection replacement undoes atomically");

    dialog.openPicker();
    search->setText("unfindable imaginary symbol family");
    check(list->model()->rowCount() == 0 && !insert->isEnabled(), "empty search disables insertion");
    key(search, Qt::Key_Return);
    check(dialog.isVisible() && editor.manuscriptText() == "One goose", "Enter with no result keeps the manuscript intact");
    search->clear();
    key(search, Qt::Key_Down);
    check(list->hasFocus(), "Down enters the grid");
    key(list, Qt::Key_M, Qt::NoModifier, "m");
    check(search->hasFocus() && search->text() == "m", "typing from the grid searches");
    key(search, Qt::Key_Escape);
    check(!dialog.isVisible() && until([&] { return editor.hasFocus(); }), "Escape restores manuscript focus");

    settings.sync();
    QSettings reopened(settings.fileName(), QSettings::IniFormat);
    SymbolPicker next(&editor, reopened, &owner);
    next.openPicker();
    auto *nextTheme = next.findChild<QComboBox *>("symbolTheme");
    auto *nextGrid = next.findChild<QListView *>("symbolGrid");
    nextTheme->setCurrentIndex(nextTheme->findText("Recently used"));
    check(nextGrid->model()->rowCount() == 2 &&
          nextGrid->currentIndex().data(Qt::UserRole).toString() == QString(QChar(0x00a0)),
          "recent symbols persist, deduplicate and sort by last insertion");
    nextTheme->setCurrentIndex(nextTheme->findText("Favorites"));
    check(nextGrid->model()->rowCount() == 1, "favorites persist across windows");
    key(nextGrid, Qt::Key_D, Qt::ControlModifier);
    check(nextGrid->model()->rowCount() == 0, "keyboard removes a favorite");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid(), "temporary settings directory");
    QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
    catalogSearch();
    picker(settings);
    std::puts("Offline symbol catalog, insertion, keyboard navigation and persistence passed.");
}
