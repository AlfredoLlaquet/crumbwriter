#include "manuscripteditor.h"
#include "navigation.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>

namespace {
void check(bool passed, const char *message)
{
    if (!passed) qFatal("%s", message);
}

QStringList displayedNames(QTreeWidget *list)
{
    QStringList names;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        names.append(list->topLevelItem(i)->text(0));
    return names;
}

void key(QWidget *target, Qt::Key code, Qt::KeyboardModifiers mods = {}, const QString &text = {})
{
    QKeyEvent down(QEvent::KeyPress, code, mods, text);
    QKeyEvent up(QEvent::KeyRelease, code, mods, text);
    QApplication::sendEvent(target, &down);
    QApplication::sendEvent(target, &up);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "temporary preference directory");
    const QString path = directory.filePath("settings.ini");
    QSettings settings(path, QSettings::IniFormat);
    QWidget window;
    QHBoxLayout layout(&window);
    ManuscriptEditor editor;
    NavigationPanel panel(&editor, &settings);
    layout.addWidget(&editor);
    layout.addWidget(&panel);
    window.show();
    window.activateWindow();
    app.processEvents();
    panel.jumpTo = [&](int position) { editor.jumpTo(position, false); editor.setFocus(); };
    editor.loadText("A Book//Crumb 1: Dream #12: a\nCrumb 2: Scene #2: b\nCrumb 3: Haiku #3: c\n"
                    "Crumb 4: Portrait #2: d");
    auto *list = panel.findChild<QTreeWidget *>("genreList");
    auto *order = panel.findChild<QToolButton *>("genreOrder");
    const QStringList alphabetical{"Dream", "Haiku", "Portrait", "Scene"};
    const QStringList byNumber{"Portrait", "Scene", "Haiku", "Dream"};
    check(!order->isChecked() && displayedNames(list) == alphabetical, "fresh genre order is alphabetical");
    list->setCurrentItem(list->topLevelItem(1));
    order->setFocus();
    key(order, Qt::Key_Space, {}, " ");
    check(order->isChecked() && displayedNames(list) == byNumber &&
          list->currentItem()->text(0) == "Haiku", "Space sorts numeric ordinals and retains the selected genre");

    const QPersistentModelIndex retained(list->currentIndex());
    auto caret = editor.textCursor();
    caret.insertText(" more writing");
    check(retained.isValid() && retained == list->currentIndex(), "a body edit does not rebuild the genre list");
    QSettings reopened(path, QSettings::IniFormat);
    ManuscriptEditor otherEditor;
    NavigationPanel other(&otherEditor, &reopened);
    otherEditor.loadText(editor.manuscriptText());
    check(displayedNames(other.findChild<QTreeWidget *>("genreList")) == byNumber,
          "new panel restores number-then-name order");
    panel.toggleGenreOrder();
    check(displayedNames(list) == alphabetical && list->currentItem()->text(0) == "Haiku",
          "second toggle restores name order and the selection");
    auto *today = panel.findChild<QLabel *>("todayCount");
    app.processEvents();
    const int crumbInformationHeight = today->parentWidget()->height();
    const QString daily = today->text();
    panel.updateCurrent();
    check(daily.startsWith("New today") && today->text() == daily, "ordinary navigation preserves the daily count");
    auto *genreSearch = panel.findChild<QLineEdit *>("genresSearch");
    panel.focusGenres();
    genreSearch->setText("#2");
    check(genreSearch->hasFocus() && displayedNames(list) == alphabetical &&
          list->currentItem()->text(0) == "Portrait", "genre number search keeps all genres visible");
    key(genreSearch, Qt::Key_Up);
    check(list->currentItem()->text(0) == "Scene", "previous genre match wraps");
    QString genreRequested;
    panel.genreOccurrences = [&](const QString &genre) { genreRequested = genre; return true; };
    const int beforeSearch = editor.model().crumbCount();
    key(genreSearch, Qt::Key_Return, Qt::ControlModifier);
    check(genreRequested == "Scene" && genreSearch->text().isEmpty() && editor.model().crumbCount() == beforeSearch,
          "Ctrl+Enter finds the selected genre without appending a crumb");
    genreSearch->setText("Scene");
    key(genreSearch, Qt::Key_Return);
    check(genreSearch->text().isEmpty() && editor.model().record(4).title == "Scene #3",
          "activation from genre search creates the next crumb and clears the field");
    editor.loadText("A Book//Crumb 1: Owl: first\nCrumb 2: : second\nCrumb 3: Owl: third");
    panel.reset();
    editor.jumpTo(0, false);
    editor.jumpTo(editor.manuscriptText().indexOf("second"), false);
    auto *history = panel.findChild<QTreeWidget *>("visitHistory");
    check(history->columnCount() == 1 && displayedNames(history) == QStringList{"Untitled", "Owl", "Owl"},
          "title-only history keeps duplicate titles at separate destinations and labels an empty title");
    auto *historySearch = panel.findChild<QLineEdit *>("historySearch");
    panel.focusHistory();
    historySearch->setText("oW");
    check(historySearch->hasFocus() && history->currentItem() == history->topLevelItem(1),
          "history search selects the first case-insensitive substring");
    key(historySearch, Qt::Key_Down);
    key(historySearch, Qt::Key_Return);
    check(editor.textCursor().blockNumber() == 2 && historySearch->text().isEmpty(),
          "second identically titled visit navigates to its own position");

    const QString longParagraph = QString(95, u'x') + QStringLiteral("🦋 the hidden word.");
    const QString novel = "Night Inventory\nA crow passed.\n\nWind\n----\n" + longParagraph +
                          "\nThe seat was empty.\n\nDawn\n----\nA visitor knocked.\nNo one answered.\n\nWind\n----\nThe bell rang.";
    editor.loadText(novel);
    panel.reset();
    const auto block = [&](int line) { return editor.document()->findBlockByNumber(line); };
    auto *todayHeading = panel.findChild<QLabel *>("todayHeading");
    check(panel.findChild<QLabel *>("genreHeading")->text() == "Titles" &&
          panel.findChild<QLabel *>("currentCrumb")->isHidden() && panel.findChild<QLabel *>("wordCount")->isHidden() &&
          panel.findChild<QLabel *>("crumbTitle")->isHidden() &&
          todayHeading->isVisible() && todayHeading->text() == "New words written today" &&
          today->text() == "0" && today->font().bold(), "generic daily words have their own figure and heading");
    const QStringList documentOrder{"Night Inventory", "Wind", "Dawn", "Wind", "End of document"};
    check(order->isChecked() && displayedNames(list) == documentOrder && list->isColumnHidden(1),
          "title list starts in document order with repeated titles retained");
    panel.focusGenres();
    genreSearch->setText("in");
    check(list->currentItem()->text(0) == "Night Inventory", "typing focuses the first matching heading");
    key(genreSearch, Qt::Key_Up);
    check(list->currentItem() == list->topLevelItem(3), "up wraps through heading matches");
    key(genreSearch, Qt::Key_Return, Qt::ControlModifier);
    check(editor.textCursor().blockNumber() == 13 && genreSearch->text().isEmpty() && editor.manuscriptText() == novel,
          "activating the later Wind moves to the heading");
    panel.toggleGenreOrder();
    check(displayedNames(list) == QStringList{"Dawn", "Night Inventory", "Wind", "Wind", "End of document"} &&
          list->currentItem() == list->topLevelItem(3), "alphabetical sorting keeps the selected duplicate heading");
    const QPersistentModelIndex heading(list->currentIndex());
    QTextCursor body(block(11));
    body.movePosition(QTextCursor::EndOfBlock);
    body.insertText(" Again.");
    check(heading.isValid() && list->currentIndex() == heading, "a prose edit preserves the title model and selection");
    panel.focusGenres();
    genreSearch->setText("missing heading");
    key(genreSearch, Qt::Key_Return);
    check(genreSearch->hasFocus() && !list->currentIndex().isValid() &&
          genreSearch->text() == "missing heading", "a missing result cannot navigate to a stale heading");
    const int beforeMissing = editor.textCursor().position();
    list->setCurrentItem(list->topLevelItem(0));
    key(list, Qt::Key_Return);
    check(editor.textCursor().position() == beforeMissing && genreSearch->text() == "missing heading",
          "moving keyboard focus into the list does not turn a missing search into an unrelated jump");
    key(genreSearch, Qt::Key_Escape);
    check(editor.hasFocus() && genreSearch->text().isEmpty(), "Escape clears panel search and returns to writing");

    panel.focusGenres();
    genreSearch->setText("end of document");
    key(genreSearch, Qt::Key_Return);
    check(editor.textCursor().position() == editor.document()->characterCount() - 1 && editor.hasFocus(),
          "the pinned title jumps to the last paragraph's end from the keyboard");

    editor.jumpTo(block(5).position() + 3, false);
    editor.jumpTo(block(6).position(), false);
    editor.jumpTo(block(7).position(), false);
    panel.focusHistory();
    historySearch->setText("hidden word");
    check(history->currentItem() && history->currentItem()->text(0) == QString(95, u'x') + QChar(0x2026) &&
          history->currentItem()->toolTip(0) == longParagraph && !displayedNames(history).contains(QString()),
          "search covers the complete visited paragraph while the list shows its opening without splitting a surrogate");
    key(historySearch, Qt::Key_Return);
    check(editor.textCursor().position() == block(5).position() + 3, "paragraph history restores the exact caret");
    QTextCursor earlier(block(1));
    earlier.insertText("Another passerby.\n");
    editor.jumpTo(block(0).position(), false);
    panel.focusHistory();
    historySearch->setText("hidden word");
    key(historySearch, Qt::Key_Return);
    check(editor.textCursor().position() == block(6).position() + 3,
          "an earlier paragraph insertion carries a saved visit to the same passage");
    earlier.setPosition(block(1).position(), QTextCursor::KeepAnchor);
    earlier.removeSelectedText();

    editor.jumpTo(block(0).position(), false);
    panel.reset();
    editor.startViSelection();
    key(&editor, Qt::Key_J, {}, "j");
    key(&editor, Qt::Key_3, {}, "3");
    check(editor.textCursor().blockNumber() == 3 && displayedNames(history) == QStringList{"Night Inventory"},
          "live movement previews leave visit history intact");
    key(&editor, Qt::Key_Escape);
    check(editor.textCursor().blockNumber() == 0 && history->topLevelItemCount() == 1,
          "canceling a movement creates no visit");
    editor.startViSelection();
    key(&editor, Qt::Key_J, {}, "j");
    key(&editor, Qt::Key_3, {}, "3");
    key(&editor, Qt::Key_Return);
    check(displayedNames(history) == QStringList{"Wind", "Night Inventory"},
          "accepting a movement records only the committed destination");

    QTextCursor prefix(block(0));
    prefix.insertText("Book//");
    check(!panel.findChild<QLabel *>("currentCrumb")->isHidden() && !order->isChecked() &&
          genreSearch->placeholderText() == "Search genres", "a live mode switch restores the independent genre order");
    prefix.setPosition(0, QTextCursor::KeepAnchor);
    prefix.removeSelectedText();
    check(!order->isChecked() && displayedNames(list).first() == "Dawn" &&
          genreSearch->placeholderText() == "Search titles", "returning to generic mode restores the title order");

    app.processEvents();
    check(today->parentWidget()->height() < crumbInformationHeight,
          "Generic mode gives the freed information-panel space to its navigation lists");

    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    editor.loadText(QStringLiteral("Book\n") + QStringLiteral("word ").repeated(1233) + "word");
    QTextCursor remove(block(1));
    remove.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    remove.removeSelectedText();
    check(today->text() == QStringLiteral("-1.234") &&
          today->toolTip().contains(today->text()) && !today->accessibleDescription().isEmpty(),
          "signed daily word totals include the desktop thousands separator even at four digits");
    panel.showTodayCountDialog();
    auto *dialog = panel.findChild<QDialog *>("todayCountDialog");
    auto *value = dialog->findChild<QLineEdit *>("todayCountValue");
    auto *resetCount = dialog->findChild<QPushButton *>("resetTodayCount");
    check(!dialog->isModal() && value->selectedText() == today->text(), "daily count is preselected in a modeless form");
    const auto *buttons = dialog->findChild<QDialogButtonBox *>();
    for (auto *button : buttons->buttons())
        if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
            check(button->isEnabled(), "the field accepts the localized, grouped count it displays");
    resetCount->click();
    check(value->text() == "0" && today->text() == "-1.234", "zero can be reviewed before accepting");
    value->setText("-42");
    key(value, Qt::Key_Return);
    check(today->text() == "-42" && !dialog->isVisible(), "Enter applies a signed generic count");
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    editor.loadText("Novel\nEnd title\n---------\n\n");
    panel.focusGenres();
    genreSearch->setText("end of document");
    key(genreSearch, Qt::Key_Return);
    check(editor.textCursor().blockNumber() == 1 && editor.textCursor().positionInBlock() == 9,
          "end destination excludes trailing blanks and the closing title marker");
    editor.loadText("Manuscript//Crumb 1: Title: Words.");
    panel.showTodayCountDialog();
    dialog = panel.findChild<QDialog *>("todayCountDialog");
    value = dialog->findChild<QLineEdit *>("todayCountValue");
    value->setText("-1");
    key(value, Qt::Key_Return);
    check(dialog->isVisible() && editor.model().todayCount() == 0, "a negative crumb count cannot be accepted");
    key(value, Qt::Key_Escape);
    check(!dialog->isVisible(), "Escape cancels a count adjustment");
    return 0;
}
