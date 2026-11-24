#pragma once

#include <QDate>
#include <QDialog>
#include <QLocale>

class CrumbModel;
class QLabel;
class QLineEdit;
class QPushButton;

class TodayCountDialog final : public QDialog {
public:
    TodayCountDialog(CrumbModel &model, QWidget *parent);
    void accept() override;

private:
    bool readValue(qint64 &value) const;
    CrumbModel &model_;
    const QDate day_;
    const QLocale numbers_;
    QLineEdit *input_;
    QLabel *message_;
    QPushButton *set_;
};
