#include "wordtools.h"
#include "wordselection.h"
#include "manuscripteditor.h"
#include "manuscriptlinks.h"
#include "manuscripttext.h"
#include "thesaurus.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>

class ThesaurusDialog final : public QDialog {
public:
    explicit ThesaurusDialog(ManuscriptEditor *editor)
        : QDialog(editor), editor_(editor)
    {
        setObjectName("offlineThesaurus");
        setWindowTitle(tr("Offline English Thesaurus"));
        resize(530, 470);
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(tr("&Look up"), this);
        query_ = new QLineEdit(this);
        query_->setObjectName("thesaurusQuery");
        label->setBuddy(query_);
        layout->addWidget(label);
        layout->addWidget(query_);
        results_ = new QTreeWidget(this);
        results_->setObjectName("thesaurusResults");
        results_->setHeaderHidden(true);
        results_->setAccessibleName(tr("Meanings and alternatives"));
        results_->setUniformRowHeights(true);
        layout->addWidget(results_, 1);
        status_ = new QLabel(this);
        status_->setObjectName("thesaurusStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        copy_ = buttons->addButton(tr("&Copy"), QDialogButtonBox::ActionRole);
        replace_ = buttons->addButton(tr("&Replace"), QDialogButtonBox::ActionRole);
        replace_->setObjectName("thesaurusReplace");
        for (auto *button : buttons->buttons())
            if (auto *push = qobject_cast<QPushButton *>(button)) push->setAutoDefault(false);
        layout->addWidget(buttons);

        query_->installEventFilter(this);
        results_->installEventFilter(this);
        connect(query_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(results_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
        connect(results_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
            if (item->parent()) replaceCurrent();
        });
        connect(replace_, &QPushButton::clicked, this, [this] { replaceCurrent(); });
        connect(copy_, &QPushButton::clicked, this, [this] { copyCurrent(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(this, &QDialog::finished, editor_, [editor] { editor->setFocus(); });
        connect(editor_->document(), &QTextDocument::contentsChanged, this, [this] {
            if (isVisible()) updateButtons();
        });
        auto *focus = new QShortcut(QKeySequence("Ctrl+L"), this);
        connect(focus, &QShortcut::activated, this, [this] { query_->setFocus(); query_->selectAll(); });
    }

    void start(const QTextCursor &target)
    {
        target_ = target;
        source_ = ManuscriptText::selectedText(target);
        {
            const QSignalBlocker block(query_);
            query_->setText(source_.simplified());
        }
        refresh();
        show();
        raise();
        activateWindow();
        if (results_->currentItem()) {
            results_->setFocus();
        } else {
            query_->setFocus();
            query_->selectAll();
        }
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress) return QDialog::eventFilter(object, event);
        auto *key = static_cast<QKeyEvent *>(event);
        if (object == results_ && key->matches(QKeySequence::Copy)) {
            copyCurrent();
            return true;
        }
        if (key->modifiers() != Qt::NoModifier) return QDialog::eventFilter(object, event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (object == query_ && (enter || key->key() == Qt::Key_Down)) {
            if (results_->currentItem()) results_->setFocus();
            return true;
        }
        if (object == results_ && enter) {
            replaceCurrent();
            return true;
        }
        return QDialog::eventFilter(object, event);
    }

private:
    void refresh()
    {
        const QSignalBlocker block(results_);
        results_->clear();
        const auto &dictionary = EnglishThesaurus::instance();
        QTreeWidgetItem *first = nullptr;
        for (const auto &sense : dictionary.lookup(query_->text())) {
            auto *heading = new QTreeWidgetItem(results_, {sense.heading});
            heading->setFlags(heading->flags() & ~Qt::ItemIsSelectable);
            for (const auto &term : sense.terms) {
                auto *item = new QTreeWidgetItem(heading, {term.display});
                item->setData(0, Qt::UserRole, term.word);
                if (!first) first = item;
            }
            heading->setExpanded(true);
        }
        if (first) results_->setCurrentItem(first);
        updateButtons();
    }

    QString currentWord() const
    {
        auto *item = results_->currentItem();
        return item ? item->data(0, Qt::UserRole).toString() : QString();
    }

    bool targetIsCurrent() const
    {
        return !source_.isEmpty() && target_.document() == editor_->document() &&
               target_.hasSelection() && ManuscriptText::selectedText(target_) == source_;
    }

    void updateButtons()
    {
        const bool hasWord = !currentWord().isEmpty();
        const bool valid = targetIsCurrent();
        copy_->setEnabled(hasWord);
        replace_->setEnabled(hasWord && valid && !editor_->isReadOnly());
        if (!EnglishThesaurus::instance().available()) {
            status_->setText(tr("The American English MyThes dictionary is missing. See the installation instructions in the README."));
        } else if (query_->text().simplified().isEmpty()) {
            status_->setText(tr("Enter a word or phrase to look up."));
        } else if (!results_->topLevelItemCount()) {
            status_->setText(tr("No entry found. Try the base form of the word."));
        } else if (!valid) {
            status_->setText(source_.isEmpty() ? tr("Select an alternative to copy.") :
                tr("The original passage has changed. Reopen the thesaurus at the word to replace."));
        } else {
            status_->setText(tr("Replace “%1”").arg(source_.simplified()));
        }
    }

    void copyCurrent()
    {
        const auto word = currentWord();
        if (!word.isEmpty()) QApplication::clipboard()->setText(word);
    }

    void replaceCurrent()
    {
        const auto word = currentWord();
        if (word.isEmpty() || editor_->isReadOnly() || !targetIsCurrent()) return;
        const int end = editor_->replaceRangeLiteral(target_.selectionStart(), target_.selectionEnd(), word);
        editor_->jumpTo(end, false);
        accept();
    }

    ManuscriptEditor *editor_;
    QTextCursor target_;
    QString source_;
    QLineEdit *query_;
    QTreeWidget *results_;
    QLabel *status_;
    QPushButton *copy_, *replace_;
};

WordTools::WordTools(ManuscriptEditor *editor) : QObject(editor), editor_(editor) {}

QTextCursor WordTools::wordAtCaret() const
{
    auto cursor = editor_->textCursor();
    cursor.clearSelection();
    if (editor_->virtualRows()) return cursor;
    return WordSelection::atCaret(cursor);
}

QTextCursor WordTools::lookupTarget() const
{
    const auto current = editor_->textCursor();
    return current.hasSelection() ? current : wordAtCaret();
}

void WordTools::selectWord()
{
    const auto word = wordAtCaret();
    if (word.hasSelection()) editor_->selectRange(word.anchor(), word.position(), false);
    else if (onNotice) onNotice(tr("No word at the caret."));
}

void WordTools::showThesaurus()
{
    if (!dialog_) dialog_ = new ThesaurusDialog(editor_);
    dialog_->start(lookupTarget());
}

void WordTools::closeThesaurus()
{
    if (dialog_ && dialog_->isVisible()) dialog_->reject();
}

QUrl WordTools::referenceUrl(Reference reference, const QString &term)
{
    const QString query = term.simplified();
    if (query.isEmpty()) return {};
    QByteArray url("https://www.merriam-webster.com/");
    url += reference == Reference::Thesaurus ? "thesaurus/" : "dictionary/";
    url += QUrl::toPercentEncoding(query);
    if (reference == Reference::Synonyms) url += "#synonyms";
    return QUrl::fromEncoded(url, QUrl::StrictMode);
}

void WordTools::openWeb(Reference reference)
{
    const auto target = lookupTarget();
    const auto url = referenceUrl(reference, ManuscriptText::selectedText(target));
    if (url.isEmpty()) {
        if (onNotice) onNotice(tr("Select a word or place the caret in one to look it up."));
    } else {
        ManuscriptLinks::open(url, editor_->window());
    }
}
