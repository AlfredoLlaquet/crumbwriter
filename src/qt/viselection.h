#pragma once

#include <QString>

class ManuscriptEditor;
class QInputMethodEvent;
class QKeyEvent;

class ViSelection {
public:
    explicit ViSelection(ManuscriptEditor &editor) : editor_(editor) {}
    bool active() const { return active_; }
    void start();
    void cancel();
    void reset();
    void key(const QKeyEvent &event);
    void inputMethod(const QInputMethodEvent &event);

private:
    void evaluate(bool enter = false);
    void prompt(const QString &hint = {}) const;
    ManuscriptEditor &editor_;
    QString command_, preedit_;
    QString lastFind_, lastTarget_;
    int lastLanding_ = -1;
    bool landedAfterTarget_ = false;
    bool active_ = false;
};
