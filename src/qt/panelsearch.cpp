#include "panelsearch.h"
#include "commandicons.h"

#include <QAbstractItemView>
#include <QKeyEvent>

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
    const int count = model->rowCount();
    int row = step > 0 ? -1 : count;
    if (fromCurrent && view_->currentIndex().isValid()) row = view_->currentIndex().row();
    for (int checked = 0; checked < count; ++checked) {
        row += step;
        if (row >= count) row = 0;
        if (row < 0) row = count - 1;
        const QModelIndex index = model->index(row, 0);
        if (!matches(index)) continue;
        view_->setCurrentIndex(index);
        view_->scrollTo(index);
        setAccessibleDescription({});
        return;
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
