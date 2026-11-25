#include "panelsearch.h"
#include "commandicons.h"

#include <QAbstractItemView>
#include <QKeyEvent>
#include <QTreeView>

namespace {
QModelIndex lastDescendant(QModelIndex index)
{
    while (index.isValid()) {
        const int children = index.model()->rowCount(index);
        if (!children) break;
        index = index.model()->index(children - 1, 0, index);
    }
    return index;
}

// Traverse the model in displayed order. Closed branches remain searchable.
QModelIndex adjacent(const QAbstractItemModel *model, QModelIndex current, bool forward)
{
    if (forward) {
        if (!current.isValid() || model->rowCount(current)) return model->index(0, 0, current);
        while (current.isValid()) {
            const auto sibling = current.siblingAtRow(current.row() + 1);
            if (sibling.isValid()) return sibling;
            current = current.parent();
        }
        return model->index(0, 0);
    }
    if (current.isValid()) {
        if (current.row() > 0) return lastDescendant(current.siblingAtRow(current.row() - 1));
        if (current.parent().isValid()) return current.parent();
    }
    return lastDescendant(model->index(model->rowCount() - 1, 0));
}
}

PanelSearch::PanelSearch(QAbstractItemView *view, QWidget *parent, int role)
    : PromptLineEdit(parent), view_(view), role_(role)
{
    CommandIcons::addSearchClearButton(this);
    view_->installEventFilter(this);
    connect(this, &QLineEdit::textChanged, this, [this](const QString &query) {
        if (query.isEmpty()) refresh();
        else seek(1, false);
    });
}

void PanelSearch::focusSearch()
{
    setFocus(Qt::ShortcutFocusReason);
    selectAll();
    refresh();
}

bool PanelSearch::matches(const QModelIndex &index) const
{
    if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled)) return false;
    if (text().isEmpty()) return true;
    const auto *model = index.model();
    for (int column = 0; column < model->columnCount(); ++column) {
        if (index.siblingAtColumn(column).data(role_).toString().contains(text(), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void PanelSearch::seek(int step, bool fromCurrent)
{
    const auto *model = view_->model();
    if (!model) return;
    const auto first = adjacent(model, fromCurrent ? view_->currentIndex().siblingAtColumn(0)
                                                   : QModelIndex(), step > 0);
    auto index = first;
    while (index.isValid()) {
        if (matches(index)) {
            if (auto *tree = qobject_cast<QTreeView *>(view_)) {
                for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
                    tree->expand(parent);
            }
            view_->setCurrentIndex(index);
            view_->scrollTo(index);
            setAccessibleDescription({});
            return;
        }
        index = adjacent(model, index, step > 0);
        if (index == first) break;
    }
    view_->selectionModel()->clear();
    setAccessibleDescription(text().isEmpty() ? QString() : tr("No matching item."));
}

void PanelSearch::refresh()
{
    if (!matches(view_->currentIndex())) seek(1, false);
}

void PanelSearch::activateCurrent(bool alternate)
{
    const auto current = view_->currentIndex();
    if (current.isValid() && (current.flags() & Qt::ItemIsEnabled) && activateRow && activateRow(alternate))
        clear();
}

bool PanelSearch::handleKey(QKeyEvent *key, bool inField)
{
    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        (mods == Qt::NoModifier || mods == Qt::ControlModifier)) {
        if (matches(view_->currentIndex())) activateCurrent(mods == Qt::ControlModifier);
        return true;
    }
    if (mods != Qt::NoModifier) return false;
    if ((inField || !text().isEmpty()) && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
        seek(key->key() == Qt::Key_Up ? -1 : 1, true);
        return true;
    }
    if (key->key() == Qt::Key_Escape && returnToEditor) {
        clear();
        returnToEditor();
        return true;
    }
    return false;
}

void PanelSearch::keyPressEvent(QKeyEvent *key)
{
    if (handleKey(key, true)) key->accept();
    else QLineEdit::keyPressEvent(key);
}

bool PanelSearch::eventFilter(QObject *object, QEvent *event)
{
    if (object == view_ && event->type() == QEvent::KeyPress &&
        handleKey(static_cast<QKeyEvent *>(event), false)) return true;
    return PromptLineEdit::eventFilter(object, event);
}
