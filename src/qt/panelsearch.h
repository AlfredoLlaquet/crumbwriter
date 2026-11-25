#pragma once

#include "promptlineedit.h"
#include <functional>

class QAbstractItemView;
class QModelIndex;

// Search a panel list or tree in place. The view keeps its original model and order.
class PanelSearch final : public PromptLineEdit {
public:
    PanelSearch(QAbstractItemView *view, QWidget *parent, int role = Qt::DisplayRole);
    void focusSearch();
    void refresh();
    void activateCurrent(bool alternate = false);
    std::function<bool(bool alternate)> activateRow;
    std::function<void()> returnToEditor;

protected:
    void keyPressEvent(QKeyEvent *key) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool matches(const QModelIndex &index) const;
    void seek(int step, bool fromCurrent);
    bool handleKey(QKeyEvent *key, bool inField);
    QAbstractItemView *view_;
    int role_;
};
