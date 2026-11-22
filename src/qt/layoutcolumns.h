#pragma once

#include <QHeaderView>
#include <QTreeView>
#include <QVariant>

// Tree/list bookmarks and Crumb/Generic mode have different applicable
// columns. Keep a writer's visibility choice even while a mode overrides it.
namespace LayoutColumns {
inline QByteArray hiddenKey(int column) { return "layoutHidden" + QByteArray::number(column); }
inline bool available(QWidget *view, int column)
{
    const QVariant unavailable = view->property("layoutUnavailableColumn");
    return !unavailable.isValid() || column != unavailable.toInt();
}
inline void apply(QWidget *view, QHeaderView *header)
{
    bool any = false;
    int first = -1;
    for (int column = 0; column < header->count(); ++column) {
        if (!available(view, column)) continue;
        if (first < 0) first = column;
        any |= !view->property(hiddenKey(column)).toBool();
    }
    for (int column = 0; column < header->count(); ++column) {
        const bool visible = available(view, column) &&
            (!view->property(hiddenKey(column)).toBool() || (!any && column == first));
        if (header->isSectionHidden(column) == visible) header->setSectionHidden(column, !visible);
    }
}
inline void setOptionalColumn(QTreeView *view, int column, bool show)
{
    view->setProperty("layoutUnavailableColumn", show ? -1 : column);
    apply(view, view->header());
}
}
