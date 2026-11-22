#pragma once

#include <QLineEdit>
#include <QPointer>

// A native line edit that makes a faint desktop placeholder readable.
class PromptLineEdit : public QLineEdit {
public:
    explicit PromptLineEdit(QWidget *parent = nullptr);
protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void watchParent();
    void queueRefresh();
    void refreshPrompt();
    QPointer<QWidget> watchedParent_;
    QPalette original_;
    QPalette corrected_;
    bool refreshing_ = false;
    bool queued_ = false;
};
