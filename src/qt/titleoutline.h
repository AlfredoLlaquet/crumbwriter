#pragma once

#include <QMap>
#include <Qt>

class CrumbModel;
class QTextDocument;
class QTreeWidget;
class QTreeWidgetItem;

// The same list widget holds crumb genres or this manuscript outline. Entries
// refer to actual text blocks even when titles repeat or an earlier line moves.
class TitleOutline {
public:
    static constexpr int endRole = Qt::UserRole + 1;
    static constexpr int searchRole = Qt::UserRole + 3;
    void rebuild(QTreeWidget *tree, const CrumbModel &model, const QTextDocument *document,
                 bool hierarchical, bool literalGuillemets);
    void clear() { items_.clear(); }
    void follow(QTreeWidget *tree, int line) const;
private:
    QMap<int, QTreeWidgetItem *> items_;
};
