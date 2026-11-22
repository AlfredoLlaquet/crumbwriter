#include "preferences.h"
#include "promptlineedit.h"
#include "synonymwriting.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
SynonymCompletion completion(const QVariant &value)
{
    if (value == "off") return SynonymCompletion::Off;
    if (value == "manual") return SynonymCompletion::OnDemand;
    return SynonymCompletion::Automatic;
}

SynonymUnderlining marking(const QVariant &value)
{
    if (value == "off") return SynonymUnderlining::Off;
    if (value == "current") return SynonymUnderlining::CurrentGroup;
    return SynonymUnderlining::All;
}
}

SynonymWritingSettings Preferences::synonymSettings() const
{
    SynonymWritingSettings result;
    result.charactersCompletion = completion(settings_.value("editor/characters-completion"));
    result.recurringCompletion = completion(settings_.value("editor/recurring-completion"));
    result.charactersUnderlining = marking(settings_.value("editor/characters-underline"));
    result.recurringUnderlining = marking(settings_.value("editor/recurring-underline"));
    result.minimumPrefix = qBound(1, settings_.value("editor/completion-prefix", 3).toInt(), 32);
    result.underlineColor = QColor(settings_.value("editor/underline-color").toString());
    return result;
}

QWidget *Preferences::synonymPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    auto addList = [&](const QString &name, const QString &prefix) {
        auto *group = new QGroupBox(name, page);
        auto *form = new QFormLayout(group);
        layout->addWidget(group);
        auto choice = [&](const QString &label, const QString &key, const QStringList &labels,
                          const QStringList &values) {
            auto *field = new QComboBox(group);
            field->setObjectName(key);
            field->setAccessibleName(name + " " + label);
            for (int i = 0; i < labels.size(); ++i) field->addItem(labels.at(i), values.at(i));
            field->setCurrentIndex(qMax(0, field->findData(settings_.value("editor/" + key))));
            form->addRow(label, field);
            QObject::connect(field, &QComboBox::currentIndexChanged, page, [this, field, key](int i) {
                set(key, field->itemData(i));
            });
        };
        choice("Completion", prefix + "-completion", {"Automatic", "On demand", "Off"},
               {"automatic", "manual", "off"});
        choice("Underlining", prefix + "-underline", {"All mentions", "Current group only", "Off"},
               {"all", "current", "off"});
    };
    addList("Characters", "characters");
    addList("Recurring", "recurring");
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *prefix = new QSpinBox(page);
    prefix->setObjectName("completion-prefix");
    prefix->setRange(1, 32);
    const auto options = synonymSettings();
    prefix->setValue(options.minimumPrefix);
    form->addRow("&Minimum prefix length", prefix);
    QObject::connect(prefix, &QSpinBox::valueChanged, page,
                     [this](int value) { set("completion-prefix", value); });

    auto *row = new QHBoxLayout;
    auto *color = new PromptLineEdit(page);
    color->setObjectName("underline-color");
    color->setAccessibleName("Underline color");
    color->setPlaceholderText("Theme color");
    color->setToolTip("Enter a color such as #6688B0. Clear the field to follow the theme.");
    if (options.underlineColor.isValid()) color->setText(options.underlineColor.name());
    auto *choose = new QPushButton("&Choose color…", page);
    row->addWidget(color);
    row->addWidget(choose);
    form->addRow("Underline color", row);
    QObject::connect(color, &QLineEdit::editingFinished, page, [this, color] {
        const QString text = color->text().trimmed();
        const QColor value(text);
        if (!text.isEmpty() && !value.isValid()) {
            color->setText(settings_.value("editor/underline-color").toString());
            return;
        }
        set("underline-color", text.isEmpty() ? text : value.name());
    });
    QObject::connect(choose, &QPushButton::clicked, page, [this, color, page] {
        if (auto *existing = page->findChild<QColorDialog *>()) {
            existing->raise();
            existing->activateWindow();
            return;
        }
        auto *dialog = new QColorDialog(page);
        dialog->setObjectName("underlineColorDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        const QColor current(color->text());
        if (current.isValid()) dialog->setCurrentColor(current);
        QObject::connect(dialog, &QColorDialog::colorSelected, color, [this, color](const QColor &value) {
            color->setText(value.name());
            set("underline-color", value.name());
        });
        dialog->show();
    });
    layout->addStretch();
    return page;
}
