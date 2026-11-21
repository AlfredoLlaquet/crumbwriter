#pragma once

#include <QDialog>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

enum class SpellAction { Previous, Next, Replace, ReplaceAll, Ignore, IgnoreAll, Add, Dictionary };

class SpellReview final : public QDialog {
public:
    explicit SpellReview(QWidget *owner);
    void showIssue(const QString &word, int line, const QStringList &suggestions);
    void setStatus(const QString &message, bool clearIssue);
    std::function<void(SpellAction, const QString &)> action;
private:
    QLabel *issue_, *status_;
    QListWidget *suggestions_;
    QLineEdit *replacement_;
    QList<QPushButton *> corrections_;
    QPushButton *replace_, *replaceAll_;
};

class SpellDictionaryDialog final : public QDialog {
public:
    explicit SpellDictionaryDialog(QWidget *owner);
    void updateWords(const QStringList &words);
    void message(const QString &text);
    std::function<void(const QString &, bool remove)> edit;
    std::function<void()> reload;
private:
    QLineEdit *entry_;
    QListWidget *words_;
    QLabel *status_;
};
