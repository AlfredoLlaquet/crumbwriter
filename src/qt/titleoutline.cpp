#include "titleoutline.h"
#include "crumbmodel.h"
#include "guillemetmarkup.h"

#include <QCollator>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTreeWidget>
#include <algorithm>
#include <array>
#include <iterator>

void TitleOutline::rebuild(QTreeWidget *tree, const CrumbModel &model, const QTextDocument *document,
                           bool hierarchical, bool literalGuillemets)
{
    QSet<int> collapsed;
    for (auto *item : items_) {
        if (item->childCount() && !item->isExpanded())
            collapsed.insert(item->data(0, Qt::UserRole).value<QTextBlock>().blockNumber());
    }
    items_.clear();
    tree->clear();
    tree->setRootIsDecorated(hierarchical);
    tree->setIndentation(tree->fontMetrics().horizontalAdvance(u'M') + 2);
    tree->setExpandsOnDoubleClick(false);
    struct Ancestor { QTreeWidgetItem *item = nullptr; QString name; };
    std::array<Ancestor, 4> hierarchy{};
    const auto lines = model.titleLines();
    QList<QTreeWidgetItem *> flat;
    if (!hierarchical) flat.reserve(lines.size());
    for (const int line : lines) {
        const auto &source = model.record(line).text;
        const auto name = !literalGuillemets && source.contains(u'«')
            ? GuillemetMarkup::visibleText(source).trimmed() : source.trimmed();
        const int level = model.headingLevel(line);
        auto *item = new QTreeWidgetItem;
        items_.insert(line, item);
        item->setData(0, searchRole, name);
        item->setData(0, Qt::UserRole, QVariant::fromValue(document->findBlockByNumber(line)));
        QStringList path;
        QTreeWidgetItem *parent = nullptr;
        for (int i = 0; i < level - 1; ++i) {
            if (hierarchy[i].item) {
                path.append(hierarchy[i].name);
                parent = hierarchy[i].item;
            }
        }
        item->setText(0, hierarchical || path.isEmpty() ? name :
                      QObject::tr("%1  ·  %2").arg(name, path.join(QStringLiteral(" › "))));
        path.append(name);
        item->setToolTip(0, path.join(QStringLiteral(" › ")));
        item->setData(0, Qt::AccessibleDescriptionRole, line == 0
            ? QObject::tr("Main Title") : QObject::tr("Level %1 heading. %2").arg(level).arg(item->toolTip(0)));
        hierarchy[level - 1] = {item, name};
        std::fill(hierarchy.begin() + level, hierarchy.end(), Ancestor{});
        if (hierarchical) {
            if (parent) parent->addChild(item);
            else tree->addTopLevelItem(item);
        } else flat.append(item);
    }
    if (hierarchical) {
        for (auto it = items_.cbegin(); it != items_.cend(); ++it)
            if (it.value()->childCount()) it.value()->setExpanded(!collapsed.contains(it.key()));
    } else {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(flat.begin(), flat.end(), [&](const auto *a, const auto *b) {
            return collator.compare(a->data(0, searchRole).toString(), b->data(0, searchRole).toString()) < 0;
        });
        tree->addTopLevelItems(flat);
    }
    auto *end = new QTreeWidgetItem(tree, {QObject::tr("End of document")});
    end->setToolTip(0, QObject::tr("Go to the end of the last paragraph."));
    end->setData(0, endRole, true);
    end->setData(0, searchRole, end->text(0));
}

void TitleOutline::follow(QTreeWidget *tree, int line) const
{
    auto it = items_.upperBound(line);
    auto *heading = it == items_.cbegin() ? nullptr : std::prev(it).value();
    if (tree->currentItem() == heading) return;
    if (heading) {
        for (auto *parent = heading->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
    }
    tree->setCurrentItem(heading);
}
