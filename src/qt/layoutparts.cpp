#include "layoutparts.h"
#include "layoutcolumns.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEvent>
#include <QHeaderView>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QWidget>

bool LayoutPart::canHide(int row) const
{
    if (!available(row)) return false;
    if (!visible(row)) return true;
    for (int i = 0; i < count(); ++i)
        if (i != row && visible(i)) return true;
    return false;
}

namespace {
class Panels final : public LayoutPart {
public:
    Panels(QSplitter *split, QObject *parent) : LayoutPart(parent), split_(split)
    {
        key = split->objectName();
        label = split->accessibleName();
        if (label.isEmpty()) label = tr("Main window panels");
        for (int i = 0; i < count(); ++i)
            split->widget(i)->installEventFilter(this);
        defaults_ = save();
        connect(split, &QSplitter::splitterMoved, this, [this] {
            split_->setProperty("layoutManualSizes", true);
            if (changed) changed();
        });
    }
    int count() const override { return split_->count(); }
    QString title(int i) const override { return split_->widget(i)->accessibleName(); }
    bool visible(int i) const override { return !split_->widget(i)->isHidden(); }
    bool available(int i) const override { return split_->widget(i)->objectName() != "manuscript"; }
    bool vertical() const override { return split_->orientation() == Qt::Vertical; }
    int size(int i) const override
    {
        const auto *pane = split_->widget(i);
        const auto real = split_->sizes().value(i);
        if (visible(i) && real > 0) return real;
        const int remembered = pane->property("layoutRememberedSize").toInt();
        if (remembered > 0) return remembered;
        return qMax(1, vertical() ? pane->sizeHint().height() : pane->sizeHint().width());
    }
    void setVisible(int i, bool show) override
    {
        if ((!show && !canHide(i)) || visible(i) == show) return;
        auto *pane = split_->widget(i);
        if (!show) pane->setProperty("layoutRememberedSize", size(i));
        auto proportions = split_->sizes();
        proportions[i] = size(i);
        pane->setVisible(show);
        if (show) split_->setSizes(proportions);
        if (changed) changed();
    }
    void move(int from, int to) override
    {
        if (from == to) return;
        auto sizes = split_->sizes();
        sizes.move(from, to);
        split_->insertWidget(to, split_->widget(from));
        split_->setSizes(sizes);
        if (changed) changed();
    }
    void resize(int row, int pixels) override
    {
        if (!visible(row)) return;
        split_->setProperty("layoutManualSizes", true);
        auto sizes = split_->sizes();
        // Give the requested pixels to (or take them from) adjacent visible
        // panes, preserving the total space assigned by the window manager.
        int extra = pixels - sizes[row];
        for (int distance = 1; distance < count() && extra; ++distance) {
            for (int peer : {row + distance, row - distance}) {
                if (peer < 0 || peer >= count() || !visible(peer)) continue;
                const int give = qMin(extra, qMax(0, sizes[peer] - 1));
                sizes[peer] -= give;
                sizes[row] += give;
                extra -= give;
            }
        }
        split_->setSizes(sizes);
        if (changed) changed();
    }
    QVariantMap save() override
    {
        QStringList order, hidden;
        QVariantMap sizes;
        for (int i = 0; i < count(); ++i) {
            auto *pane = split_->widget(i);
            const QString id = pane->objectName();
            order << id;
            if (!visible(i)) hidden << id;
            const int pixels = size(i);
            sizes.insert(id, pixels);
            pane->setProperty("layoutRememberedSize", pixels);
        }
        return {{"order", order}, {"hidden", hidden}, {"sizes", sizes},
                {"manual", split_->property("layoutManualSizes").toBool()}};
    }
    void restore(const QVariantMap &state) override
    {
        const auto order = state.value("order").toStringList();
        const auto hidden = state.value("hidden").toStringList();
        const auto sizes = state.value("sizes").toMap();
        if (order.isEmpty()) return;
        split_->setProperty("layoutManualSizes", state.value("manual", false));
        int at = 0;
        for (const auto &id : order) {
            for (int i = at; i < count(); ++i) {
                if (split_->widget(i)->objectName() != id) continue;
                if (at != i) split_->insertWidget(at, split_->widget(i));
                ++at;
                break;
            }
        }
        QList<int> weights;
        bool hasVisible = false;
        for (int i = 0; i < count(); ++i) {
            QWidget *pane = split_->widget(i);
            const QString id = pane->objectName();
            const bool show = !hidden.contains(id) || !available(i) || (i == count()-1 && !hasVisible);
            hasVisible |= show;
            const int weight = qBound(1, sizes.value(id, size(i)).toInt(), 100000);
            pane->setProperty("layoutRememberedSize", weight);
            pane->setVisible(show);
            weights << weight;
        }
        split_->setSizes(weights);
        for (int i = 0; i < count(); ++i) split_->widget(i)->updateGeometry();
        if (auto *info = split_->findChild<QWidget *>("currentInfo", Qt::FindDirectChildrenOnly))
            QCoreApplication::postEvent(info, new QEvent(QEvent::LayoutRequest));
    }
    void reset() override { restore(defaults_); }
protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if ((event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)
            && split_->isVisible() && changed) changed();
        return LayoutPart::eventFilter(object, event);
    }
private:
    QSplitter *split_;
    QVariantMap defaults_;
};

class Columns final : public LayoutPart {
public:
    Columns(QWidget *view, QHeaderView *head, QObject *parent)
        : LayoutPart(parent), view_(view), head_(head)
    {
        key = view->objectName();
        label = view->accessibleName();
        if (label.isEmpty()) label = tr("Table columns");
        head_->setSectionsMovable(true);
        defaults_ = head_->saveState();
        connect(head_, &QHeaderView::sectionMoved, this, [this] { if (changed) changed(); });
        connect(head_, &QHeaderView::sortIndicatorChanged, this, [this] { if (changed) changed(); });
        connect(head_, &QHeaderView::sectionResized, this, [this](int column, int, int pixels) {
            // A stretched/automatic column also emits this signal on every
            // window resize. Only explicit interactive widths are preferences.
            if (restoring_ || !changed || pixels <= 0 || head_->sectionResizeMode(column) != QHeaderView::Interactive ||
                (head_->stretchLastSection() && head_->visualIndex(column) == count()-1)) return;
            widths_.insert(QString::number(column), pixels);
            changed();
        });
    }
    int count() const override { return head_->count(); }
    int logical(int row) const { return head_->logicalIndex(row); }
    QString title(int row) const override
    {
        return head_->model()->headerData(logical(row), Qt::Horizontal, Qt::DisplayRole).toString();
    }
    bool visible(int row) const override { return !head_->isSectionHidden(logical(row)); }
    bool available(int row) const override
    {
        return LayoutColumns::available(view_, logical(row));
    }
    int size(int row) const override { return head_->sectionSize(logical(row)); }
    void setVisible(int row, bool show) override
    {
        if (!available(row) || (!show && !canHide(row))) return;
        const int col = logical(row);
        view_->setProperty(LayoutColumns::hiddenKey(col), !show);
        LayoutColumns::apply(view_, head_);
        if (changed) changed();
    }
    void move(int from, int to) override { head_->moveSection(from, to); }
    void resize(int row, int pixels) override
    {
        const int col = logical(row);
        if (row == count()-1) head_->setStretchLastSection(false);
        head_->setSectionResizeMode(col, QHeaderView::Interactive);
        head_->resizeSection(col, pixels);
        widths_.insert(QString::number(col), pixels);
        if (changed) changed();
    }
    QVariantMap save() override
    {
        QVariantList order, hidden;
        for (int row = 0; row < count(); ++row) {
            int col = logical(row);
            order << col;
            if (view_->property(LayoutColumns::hiddenKey(col)).toBool()) hidden << col;
        }
        return {{"order", order}, {"hidden", hidden}, {"widths", widths_},
                {"stretchLast", head_->stretchLastSection()}, {"sort", head_->sortIndicatorSection()},
                {"ascending", head_->sortIndicatorOrder() == Qt::AscendingOrder}};
    }
    void restore(const QVariantMap &state) override
    {
        const QScopedValueRollback<bool> restoring(restoring_, true);
        int row = 0;
        for (const QVariant &v : state.value("order").toList()) {
            int col = v.toInt();
            int from = head_->visualIndex(col);
            if (from < row || row >= count()) continue;
            if (from != row) head_->moveSection(from, row);
            ++row;
        }
        widths_ = state.value("widths").toMap();
        head_->setStretchLastSection(state.value("stretchLast", head_->stretchLastSection()).toBool());
        const QVariantList hidden = state.value("hidden").toList();
        for (int col = 0; col < count(); ++col) {
            view_->setProperty(LayoutColumns::hiddenKey(col), hidden.contains(col));
            auto width = widths_.constFind(QString::number(col));
            if (width != widths_.cend()) {
                head_->setSectionResizeMode(col, QHeaderView::Interactive);
                head_->resizeSection(col, qBound(1, width->toInt(), 100000));
            }
        }
        LayoutColumns::apply(view_, head_);
        const int sort = state.value("sort", -1).toInt();
        if (head_->isSortIndicatorShown() && sort >= 0 && sort < count())
            head_->setSortIndicator(sort, state.value("ascending", true).toBool() ? Qt::AscendingOrder : Qt::DescendingOrder);
    }
    void reset() override
    {
        const QScopedValueRollback<bool> restoring(restoring_, true);
        widths_.clear();
        head_->restoreState(defaults_);
        for (int row = 0; row < count(); ++row)
            view_->setProperty(LayoutColumns::hiddenKey(logical(row)), false);
        LayoutColumns::apply(view_, head_);
    }
private:
    QWidget *view_;
    QHeaderView *head_;
    QVariantMap widths_;
    QByteArray defaults_;
    bool restoring_ = false;
};
}

LayoutPart *panelLayout(QSplitter *split, QObject *parent)
{
    return new Panels(split, parent);
}

LayoutPart *columnLayout(QWidget *view, QHeaderView *header, QObject *parent)
{
    return new Columns(view, header, parent);
}
