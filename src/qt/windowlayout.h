#pragma once

#include <QObject>
#include <QHash>

class QSettings;
class QWidget;
class WindowLayoutState;

// One controller per manuscript window. Layouts belong to this computer's
// preferences. All instances sharing those preferences participate in a reset.
class WindowLayouts final : public QObject {
public:
    WindowLayouts(QWidget *manuscript, QSettings &settings);
    ~WindowLayouts() override;
    void arrange(QWidget *window);
    void restoreDefaults();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void track(QWidget *window, const QString &name);
    QWidget *manuscript_;
    QSettings &settings_;
    QHash<QWidget *, WindowLayoutState *> windows_;
};
