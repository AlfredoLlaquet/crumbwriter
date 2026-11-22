#include "windowlayout.h"
#include "layoutcolumns.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool ok, const char *requirement, int line)
{
    if (!ok) {
        std::fprintf(stderr, "Layout regression at %d: %s\n", line, requirement);
        std::exit(1);
    }
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

void events()
{
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

QWidget *pane(QSplitter *split, const char *id, const char *label)
{
    auto *w = new QWidget;
    w->setObjectName(id);
    w->setAccessibleName(label);
    split->addWidget(w);
    return w;
}

struct Fixture : QWidget {
    explicit Fixture(QSettings &settings)
    {
        setObjectName("manuscriptWindow");
        resize(650, 420);
        auto *outer = new QVBoxLayout(this);
        auto *split = new QSplitter(this);
        split->setObjectName("manuscriptSplitter");
        split->setAccessibleName("Main window panels");
        split->setChildrenCollapsible(false);
        outer->addWidget(split);
        left = pane(split, "synonymPanel", "Synonym lists");
        pane(split, "manuscript", "Manuscript");
        right = pane(split, "sidebar", "Navigation");
        split->setSizes({130, 380, 140});
        layouts = new WindowLayouts(this, settings);
        show();
        events();
    }
    WindowLayouts *layouts;
    QWidget *left, *right;
};

struct Bookmarks : QDialog {
    explicit Bookmarks(QWidget *owner) : QDialog(owner)
    {
        setObjectName("bookmarkWindow");
        auto *layout = new QVBoxLayout(this);
        mode = new QComboBox(this);
        mode->setObjectName("bookmarkViewMode");
        mode->addItems({"Tree", "List"});
        layout->addWidget(mode);
        table = new QTreeWidget(this);
        table->setObjectName("bookmarkResults");
        table->setAccessibleName("Bookmarks");
        table->setHeaderLabels({"Category", "Name", "Line"});
        table->header()->setStretchLastSection(false);
        layout->addWidget(table);
        resize(440, 240);
        show();
        events();
    }
    QTreeWidget *table;
    QComboBox *mode;
};

struct Arranger {
    Arranger(WindowLayouts *layouts, QWidget *window)
    {
        layouts->arrange(window);
        events();
        dialog = window->findChild<QDialog *>("layoutEditor", Qt::FindDirectChildrenOnly);
        CHECK(dialog && dialog->isVisible());
        items = dialog->findChild<QListWidget *>("layoutItems");
        earlier = dialog->findChild<QPushButton *>("layoutEarlier");
        pixels = dialog->findChild<QSpinBox *>("layoutSize");
        CHECK(items && earlier && pixels);
    }
    void space(int row)
    {
        items->setCurrentRow(row);
        items->setFocus();
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
        QApplication::sendEvent(items, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ");
        QApplication::sendEvent(items, &release);
        events();
    }
    QDialog *dialog;
    QListWidget *items;
    QPushButton *earlier;
    QSpinBox *pixels;
};

void panelAndColumnLayouts(QSettings &settings)
{
    int savedWidth = 0;
    {
        Fixture first(settings);
        first.activateWindow();
        first.setFocus();
        events();
        QKeyEvent shortcut(QEvent::KeyPress, Qt::Key_F10, Qt::ControlModifier | Qt::ShiftModifier);
        QApplication::sendEvent(&first, &shortcut);
        events();
        CHECK(first.findChild<QDialog *>("layoutEditor", Qt::FindDirectChildrenOnly));
        Arranger panels(first.layouts, &first);
        CHECK(panels.items->count() == 3);
        CHECK(!(panels.items->item(1)->flags() & Qt::ItemIsUserCheckable)); // Manuscript is always available.
        panels.space(0);
        CHECK(first.left->isHidden());
        panels.items->setCurrentRow(2);
        panels.earlier->click();
        CHECK(first.findChild<QSplitter *>()->widget(1) == first.right);
        panels.pixels->setValue(183);
        events();
        savedWidth = first.right->width();
        CHECK(qAbs(savedWidth - 183) < 6);
        panels.dialog->close();
        events();

        Bookmarks tool(&first);
        tool.mode->setCurrentIndex(1);
        Arranger columns(first.layouts, &tool);
        CHECK(columns.items->count() == 3);
        columns.space(1);
        columns.items->setCurrentRow(2);
        columns.earlier->click();
        columns.pixels->setValue(147);
        columns.dialog->close();
        tool.resize(501, 312);
        events();
        tool.hide();
        first.hide();
        events();
    }
    Fixture second(settings);
    CHECK(second.left->isHidden());
    CHECK(second.findChild<QSplitter *>()->widget(1) == second.right);
    CHECK(qAbs(second.right->width() - savedWidth) < 8);
    Bookmarks restored(&second);
    CHECK(restored.mode->currentIndex() == 1);
    CHECK(restored.table->isColumnHidden(1));
    CHECK(restored.table->header()->visualIndex(2) == 1);
    CHECK(restored.table->columnWidth(2) == 147);
    CHECK(restored.size() == QSize(501, 312));

    // Reset propagates to another manuscript with a hidden tool, even if a
    // resize timer was pending there. Other preferences survive the reset.
    Fixture other(settings);
    Bookmarks oldTool(&other);
    oldTool.hide();
    settings.setValue("editor/font-size", 18);
    second.resize(579, 353);
    events();
    second.layouts->restoreDefaults();
    events();
    CHECK(second.size() == QSize(579, 353)); // Keep the live compositor's tile.
    Fixture newMain(settings);
    CHECK(newMain.size() == QSize(650, 420)); // New floating window starts at the factory size.
    CHECK(!second.left->isHidden() && !other.left->isHidden());
    CHECK(second.findChild<QSplitter *>()->widget(2) == second.right);
    CHECK(!restored.table->isColumnHidden(1));
    CHECK(restored.table->header()->visualIndex(2) == 2);
    CHECK(restored.mode->currentIndex() == 0);
    oldTool.show();
    events();
    CHECK(!oldTool.table->isColumnHidden(1));
    CHECK(oldTool.table->header()->visualIndex(2) == 2);
    CHECK(settings.value("editor/font-size").toInt() == 18);
}

void optionalColumns()
{
    QTreeWidget table;
    table.setHeaderLabels({"Name", "Number"});
    table.setProperty(LayoutColumns::hiddenKey(0), true);
    LayoutColumns::setOptionalColumn(&table, 1, true);
    CHECK(table.isColumnHidden(0) && !table.isColumnHidden(1));
    // The sole remaining heading becomes visible when switching to a mode
    // where numbers do not exist. Returning to Crumb mode restores the choice.
    LayoutColumns::setOptionalColumn(&table, 1, false);
    CHECK(!table.isColumnHidden(0) && table.isColumnHidden(1));
    LayoutColumns::setOptionalColumn(&table, 1, true);
    CHECK(table.isColumnHidden(0) && !table.isColumnHidden(1));
}

void unavailableDisplay(QSettings &settings)
{
    const auto key = "layouts/v1/manuscriptWindow/geometry";
    settings.setValue(key, QVariantMap{{"size", QSize(30000, 30000)}, {"screen", "missing monitor"},
                                      {"position", QPoint(25000, 20000)}});
    Fixture window(settings);
    CHECK(window.size().width() <= window.screen()->availableSize().width());
    CHECK(window.size().height() <= window.screen()->availableSize().height());
    window.resize(575, 385);
    events();
    CHECK(window.size() == QSize(575, 385));
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QSettings settings(tmp.filePath("settings.ini"), QSettings::IniFormat);
    panelAndColumnLayouts(settings);
    optionalColumns();
    unavailableDisplay(settings);
    return 0;
}
