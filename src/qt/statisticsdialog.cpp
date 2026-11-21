#include "statisticsdialog.h"
#include "crumbmodel.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

StatisticsDialog::StatisticsDialog(CrumbModel &model, QWidget *parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("statisticsDialog");
    setWindowTitle(tr("Manuscript Statistics"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto value = [&](const QString &caption, const char *name) {
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->setObjectName(name);
        label->setAccessibleName(caption);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(caption, label);
        return label;
    };
    crumbs_ = value(tr("Crumbs"), "statisticsCrumbs");
    words_ = value(tr("Total words"), "statisticsWords");
    average_ = value(tr("Average words per crumb"), "statisticsAverage");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(tr("&Copy"), QDialogButtonBox::ActionRole);
    const auto copySummary = [this] { QApplication::clipboard()->setText(summary()); };
    connect(copy, &QPushButton::clicked, this, copySummary);
    auto *shortcut = new QShortcut(QKeySequence::Copy, this);
    connect(shortcut, &QShortcut::activated, this, copySummary);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
    refresh(model);
    model.observe(this, [this, &model](const CrumbChange &change) {
        if (change.textChanged) refresh(model);
    });
}

void StatisticsDialog::refresh(const CrumbModel &model)
{
    const QLocale locale;
    const int count = model.crumbCount();
    const auto total = model.totalWords();
    crumbs_->setText(locale.toString(count));
    words_->setText(locale.toString(total));
    average_->setText(locale.toString(count ? double(total) / count : 0.0, 'f', 2));
}

QString StatisticsDialog::summary() const
{
    return tr("Crumbs: %1\nTotal words: %2\nAverage words per crumb: %3")
        .arg(crumbs_->text(), words_->text(), average_->text());
}
