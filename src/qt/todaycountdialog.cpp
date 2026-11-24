#include "todaycountdialog.h"
#include "crumbmodel.h"
#include "manuscriptnumbers.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

TodayCountDialog::TodayCountDialog(CrumbModel &model, QWidget *parent)
    : QDialog(parent), model_(model), day_(QDate::currentDate()), numbers_(ManuscriptNumbers::locale())
{
    setObjectName("todayCountDialog");
    setWindowTitle(tr("Set Today’s Count"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);
    auto *label = new QLabel(model.isGeneric() ? tr("New &words written today") : tr("New &crumbs written today"), this);
    layout->addWidget(label);
    input_ = new QLineEdit(this);
    input_->setObjectName("todayCountValue");
    input_->setText(ManuscriptNumbers::format(model.isGeneric() ? model.todayWords() : model.todayCount()));
    label->setBuddy(input_);
    layout->addWidget(input_);
    message_ = new QLabel(this);
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *reset = buttons->addButton(tr("&Reset to Zero"), QDialogButtonBox::ResetRole);
    reset->setObjectName("resetTodayCount");
    set_ = buttons->addButton(tr("&Set"), QDialogButtonBox::AcceptRole);
    set_->setDefault(true);
    layout->addWidget(buttons);
    connect(reset, &QPushButton::clicked, this, [this] {
        input_->setText(numbers_.toString(0));
        input_->setFocus();
        input_->selectAll();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &TodayCountDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto validate = [this] {
        qint64 value;
        const bool valid = readValue(value);
        set_->setEnabled(valid);
        message_->setText(valid ? tr("Applies to this manuscript for today.") :
                          model_.isGeneric() ? tr("Enter a whole number, including a negative value if needed.") :
                                               tr("Enter zero or a positive whole number."));
    };
    connect(input_, &QLineEdit::textChanged, this, validate);
    validate();
    input_->setFocus();
    input_->selectAll();
    resize(380, sizeHint().height());
    model.observe(this, [this](const CrumbChange &) {
        if (day_ != QDate::currentDate()) reject();
    });
}

bool TodayCountDialog::readValue(qint64 &value) const
{
    bool ok;
    const QString text = input_->text().trimmed();
    value = numbers_.toLongLong(text, &ok);
    if (!ok && text.contains(numbers_.groupSeparator())) {
        // The manuscript groups four digits even in locales that normally
        // start at five. Accept that exact spelling, including its sign.
        QString digits = text;
        digits.remove(numbers_.groupSeparator());
        value = numbers_.toLongLong(digits, &ok);
        ok = ok && ManuscriptNumbers::grouped(numbers_.toString(value), numbers_) == text;
    }
    return ok && (model_.isGeneric() || value >= 0);
}

void TodayCountDialog::accept()
{
    if (day_ != QDate::currentDate()) { reject(); return; }
    qint64 value;
    if (!readValue(value)) return;
    const QString error = model_.setTodayCount(value);
    if (!error.isEmpty()) { message_->setText(error); return; }
    QDialog::accept();
}
