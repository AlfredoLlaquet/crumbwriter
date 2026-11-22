#pragma once

#include <QDialog>

class CrumbModel;
class QLabel;

class StatisticsDialog final : public QDialog {
public:
    explicit StatisticsDialog(CrumbModel &model, QWidget *parent);
private:
    void refresh(const CrumbModel &model);
    QString summary() const;
    QLabel *crumbs_;
    QLabel *unitCaption_;
    QLabel *averageCaption_;
    QLabel *words_;
    QLabel *average_;
};
