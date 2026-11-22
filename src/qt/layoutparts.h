#pragma once

#include <QObject>
#include <QVariant>
#include <functional>

class QHeaderView;
class QSplitter;
class QWidget;

// An arranger row denotes a physical pane or a logical model column. The
// dialog lists rows in visual order, including those that are currently hidden.
class LayoutPart : public QObject {
public:
    using QObject::QObject;
    QString key, label;
    std::function<void()> changed;
    virtual int count() const = 0;
    virtual QString title(int row) const = 0;
    virtual bool visible(int row) const = 0;
    virtual bool available(int row) const = 0;
    virtual bool vertical() const { return false; }
    virtual int size(int row) const = 0;
    virtual void setVisible(int row, bool show) = 0;
    virtual void move(int from, int to) = 0;
    virtual void resize(int row, int pixels) = 0;
    virtual QVariantMap save() = 0;
    virtual void restore(const QVariantMap &state) = 0;
    virtual void reset() = 0;
    bool canHide(int row) const;
};

LayoutPart *panelLayout(QSplitter *split, QObject *parent);
LayoutPart *columnLayout(QWidget *view, QHeaderView *header, QObject *parent);
void showLayoutEditor(QWidget *owner, const QList<LayoutPart *> &parts);
