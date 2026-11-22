#include "spellreview.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace {
QLabel *plainLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
}

SpellReview::SpellReview(QWidget *owner) : QDialog(owner)
{
    setWindowTitle(tr("Spelling — American English"));
    setObjectName("spellingReview");
    auto *layout = new QVBoxLayout(this);
    issue_ = plainLabel(this);
    issue_->setObjectName("spellingIssue");
    layout->addWidget(issue_);
    auto *replacementLabel = new QLabel(tr("Replacemen&t"), this);
    replacement_ = new QLineEdit(this);
    replacement_->setObjectName("spellingReplacement");
    replacementLabel->setBuddy(replacement_);
    layout->addWidget(replacementLabel);
    layout->addWidget(replacement_);
    auto *suggestionsLabel = new QLabel(tr("Su&ggestions"), this);
    suggestions_ = new QListWidget(this);
    suggestions_->setObjectName("spellingSuggestions");
    suggestionsLabel->setBuddy(suggestions_);
    layout->addWidget(suggestionsLabel);
    layout->addWidget(suggestions_);

    auto *commands = new QGridLayout;
    layout->addLayout(commands);
    auto addButton = [&](const QString &caption, const char *name, SpellAction operation, int row, int column) {
        auto *button = new QPushButton(caption, this);
        button->setObjectName(name);
        button->setAutoDefault(false);
        commands->addWidget(button, row, column);
        connect(button, &QPushButton::clicked, this, [this, operation] {
            if (action) action(operation, replacement_->text());
        });
        return button;
    };
    replace_ = addButton(tr("&Replace"), "spellingReplace", SpellAction::Replace, 0, 0);
    replaceAll_ = addButton(tr("Replace &All"), "spellingReplaceAll", SpellAction::ReplaceAll, 0, 1);
    corrections_ = {
        addButton(tr("&Ignore Once"), "spellingIgnore", SpellAction::Ignore, 1, 0),
        addButton(tr("Ignore A&ll"), "spellingIgnoreAll", SpellAction::IgnoreAll, 1, 1),
        addButton(tr("Add to &Dictionary"), "spellingAdd", SpellAction::Add, 2, 0)
    };
    addButton(tr("Dictionary &Words…"), "spellingDictionary", SpellAction::Dictionary, 2, 1);
    addButton(tr("&Previous (Shift+F8)"), "spellingPrevious", SpellAction::Previous, 3, 0);
    addButton(tr("&Next (F8)"), "spellingNext", SpellAction::Next, 3, 1);
    auto bind = [&](QKeySequence key, SpellAction operation) {
        auto *shortcut = new QShortcut(key, this);
        connect(shortcut, &QShortcut::activated, this, [this, operation] {
            if (action) action(operation, {});
        });
    };
    bind(QKeySequence(Qt::Key_F8), SpellAction::Next);
    bind(QKeySequence(Qt::SHIFT | Qt::Key_F8), SpellAction::Previous);
    status_ = plainLabel(this);
    layout->addWidget(status_);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    close->button(QDialogButtonBox::Close)->setAutoDefault(false);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(replacement_, &QLineEdit::textChanged, this, [this](const QString &text) {
        const bool canCorrect = replacement_->isEnabled() && !text.trimmed().isEmpty();
        replace_->setEnabled(canCorrect);
        replaceAll_->setEnabled(canCorrect);
    });
    connect(replacement_, &QLineEdit::returnPressed, replace_, &QPushButton::click);
    connect(suggestions_, &QListWidget::currentTextChanged, replacement_, &QLineEdit::setText);
    connect(suggestions_, &QListWidget::itemActivated, this, [this] { replace_->click(); });
    setStatus({}, true);
}

void SpellReview::setStatus(const QString &message, bool clearIssue)
{
    status_->setText(message);
    if (!clearIssue) return;
    issue_->clear();
    suggestions_->clear();
    replacement_->setEnabled(false);
    replacement_->clear();
    for (auto *button : corrections_) button->setEnabled(false);
    replace_->setEnabled(false);
    replaceAll_->setEnabled(false);
}

void SpellReview::showIssue(const QString &word, int line, const QStringList &suggestions)
{
    status_->clear();
    issue_->setText(tr("Line %1 — %2").arg(line).arg(word));
    suggestions_->clear();
    suggestions_->addItems(suggestions);
    for (auto *button : corrections_) button->setEnabled(true);
    replacement_->setEnabled(true);
    if (suggestions.isEmpty()) replacement_->setText(word);
    else suggestions_->setCurrentRow(0);
    replace_->setEnabled(!replacement_->text().isEmpty());
    replaceAll_->setEnabled(!replacement_->text().isEmpty());
    replacement_->setFocus();
    replacement_->selectAll();
}

SpellDictionaryDialog::SpellDictionaryDialog(QWidget *owner) : QDialog(owner)
{
    setWindowTitle(tr("Manuscript Dictionary"));
    setObjectName("manuscriptDictionary");
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(tr("&Word or filter"), this);
    entry_ = new QLineEdit(this);
    entry_->setObjectName("dictionaryWord");
    label->setBuddy(entry_);
    layout->addWidget(label);
    layout->addWidget(entry_);
    words_ = new QListWidget(this);
    words_->setObjectName("dictionaryWords");
    words_->setAccessibleName(tr("Words saved for this manuscript"));
    layout->addWidget(words_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *add = buttons->addButton(tr("&Add"), QDialogButtonBox::ActionRole);
    auto *remove = buttons->addButton(tr("&Remove"), QDialogButtonBox::ActionRole);
    auto *read = buttons->addButton(tr("Re&load"), QDialogButtonBox::ActionRole);
    for (auto *button : buttons->buttons())
        if (auto *push = qobject_cast<QPushButton *>(button)) push->setAutoDefault(false);
    status_ = plainLabel(this);
    layout->addWidget(status_);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, [this] {
        if (edit && !entry_->text().trimmed().isEmpty()) edit(entry_->text(), false);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const auto *row = words_->currentItem();
        if (edit && row && !row->isHidden()) edit(row->text(), true);
    });
    connect(read, &QPushButton::clicked, this, [this] { if (reload) reload(); });
    connect(entry_, &QLineEdit::returnPressed, add, &QPushButton::click);
    connect(entry_, &QLineEdit::textChanged, this, [this](const QString &filter) {
        for (int row = 0; row < words_->count(); ++row)
            words_->item(row)->setHidden(!words_->item(row)->text().contains(filter, Qt::CaseInsensitive));
    });
    auto *refresh = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refresh, &QShortcut::activated, read, &QPushButton::click);
    auto *del = new QShortcut(QKeySequence(Qt::Key_Delete), words_);
#ifdef Q_OS_MACOS
    del->setKeys({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
#endif
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, remove, &QPushButton::click);
}

void SpellDictionaryDialog::updateWords(const QStringList &words)
{
    words_->clear();
    words_->addItems(words);
    const auto filter = entry_->text();
    for (int i = 0; i < words_->count(); ++i)
        words_->item(i)->setHidden(!words_->item(i)->text().contains(filter, Qt::CaseInsensitive));
}

void SpellDictionaryDialog::message(const QString &text) { status_->setText(text); }
