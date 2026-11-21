#include "statisticsdialog.h"
#include "crumbmodel.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>

namespace {
QString grouped(QString text, const QLocale &locale)
{
    // Some desktop locales, including Spanish, start grouping at 10,000.
    // Show the requested separator for four-digit manuscript totals too.
    const auto decimal = text.indexOf(locale.decimalPoint());
    const auto integerLength = decimal < 0 ? text.size() : decimal;
    const auto digitLength = locale.zeroDigit().size();
    if (integerLength == 4 * digitLength && !text.contains(locale.groupSeparator()))
        text.insert(digitLength, locale.groupSeparator());
    return text;
}
}

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
    layout->addLayout(form);
    auto value = [&](int row, const QString &caption, const char *name) {
        auto *description = new QLabel(caption, this);
        description->setWordWrap(true);
        form->addWidget(description, row, 0);
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->setObjectName(name);
        label->setAccessibleName(caption);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        form->addWidget(label, row, 1);
        return label;
    };
    crumbs_ = value(0, tr("Crumbs"), "statisticsCrumbs");
    words_ = value(1, tr("Total words"), "statisticsWords");
    average_ = value(2, tr("Average words per crumb"), "statisticsAverage");
    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(tr("&Copy"), QDialogButtonBox::ActionRole);
    const auto copySummary = [this] { QApplication::clipboard()->setText(summary()); };
    connect(copy, &QPushButton::clicked, this, copySummary);
    auto *shortcut = new QShortcut(QKeySequence::Copy, this);
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
    QLocale locale;
    if (locale.language() == QLocale::C)
        locale = QLocale(QLocale::English, QLocale::UnitedStates);
    locale.setNumberOptions(locale.numberOptions() & ~QLocale::OmitGroupSeparator);
    const int count = model.crumbCount();
    const auto total = model.totalWords();
    crumbs_->setText(grouped(locale.toString(count), locale));
    words_->setText(grouped(locale.toString(total), locale));
    average_->setText(grouped(locale.toString(count ? double(total) / count : 0.0, 'f', 2), locale));
}

QString StatisticsDialog::summary() const
{
    return tr("Crumbs: %1\nTotal words: %2\nAverage words per crumb: %3")
        .arg(crumbs_->text(), words_->text(), average_->text());
}
