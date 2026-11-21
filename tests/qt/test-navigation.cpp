#include "manuscripteditor.h"
#include "navigation.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QTemporaryDir>
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

void pressSpace(QWidget *target)
{
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ");
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
    editor.loadText("Crumb 1: Dream #12: a\nCrumb 2: Scene #2: b\nCrumb 3: Haiku #3: c\n"
                    "Crumb 4: Portrait #2: d");
    auto *list = panel.findChild<QTreeWidget *>("genreList");
    auto *order = panel.findChild<QToolButton *>("genreOrder");
    const QStringList alphabetical{"Dream", "Haiku", "Portrait", "Scene"};
    const QStringList byNumber{"Portrait", "Scene", "Haiku", "Dream"};
    check(!order->isChecked() && displayedNames(list) == alphabetical, "fresh genre order is alphabetical");
    list->setCurrentItem(list->topLevelItem(1));
    order->setFocus();
    pressSpace(order);
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
    panel.setTodayCount(12);
    panel.updateCurrent();
    check(panel.findChild<QLabel *>("todayCount")->text() == "New today   12",
          "ordinary navigation preserves the daily count");
    editor.loadText("Crumb 1: Owl: first\nCrumb 2: : second\nCrumb 3: Owl: third");
    panel.reset();
    editor.jumpTo(0, false);
    editor.jumpTo(editor.manuscriptText().indexOf("second"), false);
    auto *history = panel.findChild<QTreeWidget *>("visitHistory");
    check(history->columnCount() == 1 && displayedNames(history) == QStringList{"Untitled", "Owl", "Owl"},
          "title-only history keeps duplicate titles at separate destinations and labels an empty title");
    return 0;
}
