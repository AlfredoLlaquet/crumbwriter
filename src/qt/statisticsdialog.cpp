#include "statisticsdialog.h"
#include "crumbmodel.h"
#include "manuscriptnumbers.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>

StatisticsDialog::StatisticsDialog(CrumbModel &model, QWidget *parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("statisticsDialog");
    setWindowTitle(tr("Manuscript Statistics"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(18);
    auto *form = new QGridLayout;
    form->setColumnStretch(0, 1);
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);
    layout->addLayout(form, 1);
    auto value = [&](int row, QLabel *description, const char *name) {
        description->setWordWrap(true);
        form->addWidget(description, row, 0);
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->setObjectName(name);
        label->setAccessibleName(description->text());
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto font = label->font();
        font.setPointSizeF(label->fontInfo().pointSizeF() * 1.2);
        font.setBold(true);
        label->setFont(font);
        form->addWidget(label, row, 1);
        form->setRowStretch(row, 1);
        return label;
    };
    unitCaption_ = new QLabel(this);
    averageCaption_ = new QLabel(this);
    crumbs_ = value(0, unitCaption_, "statisticsCrumbs");
    words_ = value(1, new QLabel(tr("Total words"), this), "statisticsWords");
    average_ = value(2, averageCaption_, "statisticsAverage");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(tr("&Copy"), QDialogButtonBox::ActionRole);
    const auto copySummary = [this] { QApplication::clipboard()->setText(summary()); };
    connect(copy, &QPushButton::clicked, this, copySummary);
    auto *shortcut = new QShortcut(QKeySequence::Copy, this);
#ifdef Q_OS_MACOS
    shortcut->setKeys({QKeySequence(Qt::CTRL | Qt::Key_C), QKeySequence(Qt::META | Qt::Key_C)});
#endif
    connect(shortcut, &QShortcut::activated, this, copySummary);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
    refresh(model);
    resize(QSize(480, 240).boundedTo(screen()->availableGeometry().size() * 0.85));
    model.observe(this, [this, &model](const CrumbChange &change) {
        if (change.textChanged) refresh(model);
    });
}

void StatisticsDialog::refresh(const CrumbModel &model)
{
    const bool generic = model.isGeneric();
    unitCaption_->setText(generic ? tr("Paragraphs") : tr("Crumbs"));
    averageCaption_->setText(generic ? tr("Average words per paragraph") : tr("Average words per crumb"));
    crumbs_->setAccessibleName(unitCaption_->text());
    average_->setAccessibleName(averageCaption_->text());
    const int count = generic ? model.paragraphCount() : model.crumbCount();
    const auto total = model.totalWords();
    crumbs_->setText(ManuscriptNumbers::format(count));
    words_->setText(ManuscriptNumbers::format(total));
    average_->setText(ManuscriptNumbers::fixed(count ? double(total) / count : 0.0, 2));
}

QString StatisticsDialog::summary() const
{
    return tr("%1: %2\nTotal words: %3\n%4: %5")
        .arg(unitCaption_->text(), crumbs_->text(), words_->text(), averageCaption_->text(), average_->text());
}
