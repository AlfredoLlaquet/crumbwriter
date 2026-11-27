#include "synonympanel.h"
#include "commandicons.h"
#include "panelsearch.h"
#include "synonymfile.h"
#include "synonymmatch.h"
#include "synonymresults.h"

#include <QAbstractListModel>
#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
class GroupModel final : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    void setGroups(const QList<SynonymEntry> &groups)
    {
        beginResetModel();
        groups_ = groups;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(groups_.size());
    }
    QVariant data(const QModelIndex &i, int role) const override
    {
        if (!i.isValid() || i.row() >= groups_.size()) return {};
        const auto &entry = groups_[i.row()];
        const auto &group = entry.aliases;
        if (role == Qt::UserRole) return group.join(u'\n'); // Search the unescaped aliases.
        if (role == Qt::ToolTipRole && entry.excludeFromAutocomplete)
            return SynonymFile::format(group) + tr("\nExcluded from autocomplete");
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
            return SynonymFile::format(group);
        return {};
    }

private:
    QList<SynonymEntry> groups_;
};

void showWindow(QWidget *w)
{
    w->show();
    w->raise();
    w->activateWindow();
}

bool report(const QStringList &errors, QString *error)
{
    if (error) *error = errors.join(u'\n');
    return errors.isEmpty();
}
}

class SynonymPanel::Section final : public QWidget {
public:
    Section(SynonymPanel &owner, const QString &title, const QString &suffix, const QString &keys)
        : QWidget(&owner), file(suffix), owner_(owner), name_(title), id_(suffix.mid(1)),
          model_(new GroupModel(this)), list_(new QListView(this)),
          search_(new PanelSearch(list_, this, Qt::UserRole)), notice_(new QLabel(this))
    {
        setObjectName(id_ + QStringLiteral("Section"));
        setAccessibleName(title);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        auto *header = new QHBoxLayout;
        auto *label = new QLabel(title, this);
        QFont font;
        font.setBold(true);
        label->setFont(font);
        label->setBuddy(search_);
        header->addWidget(label);
        header->addStretch();
        const QString actionsLabel = title + QStringLiteral(" actions");
        auto *options = CommandIcons::menuButton(this, actionsLabel, actionsLabel);
        options->setObjectName(id_ + QStringLiteral("Menu"));
        auto *menu = new QMenu(options);
        options->setMenu(menu);
        auto action = [&](const QString &text, auto method) {
            auto *a = menu->addAction(text);
            connect(a, &QAction::triggered, this, method);
            return a;
        };
#ifdef Q_OS_MACOS
        action(QStringLiteral("&Add…\tCtrl+I"), [this] { edit(true); });
#else
        action(QStringLiteral("&Add…\tInsert"), [this] { edit(true); });
#endif
        auto *rename = action(QStringLiteral("&Edit…\tF2"), [this] { edit(false); });
        auto *remove = action(QStringLiteral("&Delete…\tDelete"), [this] { askDelete(); });
        menu->addSeparator();
        auto *find = action(QStringLiteral("&Find all occurrences\tEnter"), [this] { search_->activateCurrent(); });
        auto *insert = action(QStringLiteral("&Insert at caret\tCtrl+Enter"), [this] { search_->activateCurrent(true); });
        menu->addSeparator();
        auto *reload = action(QStringLiteral("&Reload list\tF5"), [this] { reloadList(); });
        connect(menu, &QMenu::aboutToShow, this, [this, rename, remove, find, insert, reload] {
            const bool selected = currentRow() >= 0;
            for (auto *a : {rename, remove, find, insert}) a->setEnabled(selected);
            reload->setEnabled(!file.manuscriptPath().isEmpty());
        });
        header->addWidget(options);
        layout->addLayout(header);
        search_->setObjectName(id_ + QStringLiteral("Search"));
        search_->setPlaceholderText(tr("Search %1").arg(id_));
        search_->setAccessibleName(search_->placeholderText());
        search_->setToolTip(tr("Search any synonym (%1). Up and Down move between matches. "
                               "Enter finds occurrences. Ctrl+Enter inserts the first synonym.").arg(keys));
        search_->installEventFilter(this);
        layout->addWidget(search_);
        list_->setObjectName(id_ + QStringLiteral("List"));
        list_->setAccessibleName(title);
        list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list_->setUniformItemSizes(true);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_->setMinimumSize(0, 0);
        list_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        list_->installEventFilter(this);
        list_->setModel(model_);
        list_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list_, &QWidget::customContextMenuRequested, this, [this, menu](QPoint point) {
            const auto index = list_->indexAt(point);
            if (!index.isValid()) return;
            list_->setCurrentIndex(index);
            menu->popup(list_->viewport()->mapToGlobal(point));
        });
        search_->activateRow = [this](bool inserting) { return use(inserting); };
        search_->returnToEditor = [this] { if (owner_.returnToEditor) owner_.returnToEditor(); };
        connect(list_, &QListView::activated, this, [this] { search_->activateCurrent(); });
        setTabOrder(options, search_);
        setTabOrder(search_, list_);
        layout->addWidget(list_, 1);
        notice_->setTextFormat(Qt::PlainText);
        notice_->setWordWrap(true);
        notice_->hide();
        layout->addWidget(notice_);
    }

    SynonymFile file;

    void focusSearch()
    {
        search_->focusSearch();
    }
    void updateList(const SynonymEntry &select = {})
    {
        model_->setGroups(file.groups());
        if (!select.aliases.isEmpty()) list_->setCurrentIndex(model_->index(file.groups().indexOf(select), 0));
        search_->refresh();
    }
    void clearView()
    {
        if (form_) form_->close();
        if (question_) question_->close();
        search_->clear();
        notice_->hide();
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
        const int code = key->key();
#ifdef Q_OS_MACOS
        if (mods == Qt::ControlModifier && code == Qt::Key_I) { edit(true); return true; }
        if (!mods && code == Qt::Key_Backspace && object == list_) { askDelete(); return true; }
#endif
        if (mods != Qt::NoModifier) return false;
        if (code == Qt::Key_Insert) { edit(true); return true; }
        if (code == Qt::Key_F2) { edit(false); return true; }
        if (code == Qt::Key_Delete && object == list_) { askDelete(); return true; }
        if (code == Qt::Key_F5) { reloadList(); return true; }
        if (object == list_ && !key->text().isEmpty() && key->text().at(0).isPrint()) {
            focusSearch();
            search_->insert(key->text());
            return true;
        }
        return false;
    }

private:
    int currentRow() const
    {
        return list_->currentIndex().row();
    }
    void showError(const QString &error)
    {
        notice_->setText(error);
        notice_->setVisible(!error.isEmpty());
    }
    bool use(bool inserting)
    {
        const int row = currentRow();
        if (row < 0 || row >= file.groups().size()) return false;
        const auto &group = file.groups().at(row).aliases;
        if (inserting) {
            if (!owner_.insertSynonym) return false;
            owner_.insertSynonym(group.first());
        } else {
            if (!owner_.document_) return false;
            const int offset = this == owner_.recurring_ ? int(owner_.characters_->file.groups().size()) : 0;
            owner_.showOccurrences(offset + row);
        }
        return true;
    }
    void edit(bool adding)
    {
        if (form_) { showWindow(form_); return; }
        const int row = currentRow();
        if (!adding && (row < 0 || row >= file.groups().size())) return;
        const auto original = adding ? SynonymEntry{} : file.groups().at(row);
        auto *form = new QDialog(this);
        form_ = form;
        form->setAttribute(Qt::WA_DeleteOnClose);
        form->setObjectName(id_ + QStringLiteral("EditDialog"));
        form->setWindowTitle((adding ? QStringLiteral("Add synonym group — ") :
                                       QStringLiteral("Edit synonym group — ")) + name_);
        auto *layout = new QVBoxLayout(form);
        auto *label = new QLabel(QStringLiteral("&Synonyms"), form);
        auto *field = new QLineEdit(form);
        field->setObjectName(QStringLiteral("synonymText"));
        field->setAccessibleName(QStringLiteral("Synonyms"));
        label->setBuddy(field);
        if (!adding) {
            field->setText(SynonymFile::format(original.aliases));
        } else if (owner_.manuscriptSelection) {
            QString name = owner_.manuscriptSelection().trimmed();
            name.replace(u'\n', u' ');
            if (!name.isEmpty()) field->setText(SynonymFile::format({name}));
        }
        auto *hint = new QLabel(QStringLiteral("Separate names with commas. Enclose a name containing a comma in double quotes."), form);
        hint->setWordWrap(true);
        auto *excluded = new QCheckBox(tr("&Exclude from autocomplete"), form);
        excluded->setObjectName(QStringLiteral("excludeFromAutocomplete"));
        excluded->setToolTip(tr("Keep these names available for marking, navigation, spelling, and manual insertion."));
        excluded->setChecked(original.excludeFromAutocomplete);
        auto *error = new QLabel(form);
        error->setTextFormat(Qt::PlainText);
        error->setWordWrap(true);
        error->hide();
        layout->addWidget(label);
        layout->addWidget(field);
        layout->addWidget(hint);
        layout->addWidget(excluded);
        layout->addWidget(error);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, form);
        buttons->button(QDialogButtonBox::Save)->setDefault(true);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, form, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, form,
                [this, adding, row, original, form, field, excluded, error] {
            QString reason;
            QStringList parsed;
            const bool fresh = adding || file.groups().value(row) == original;
            if (!fresh) reason = QStringLiteral("The selected group changed. Reopen its editor.");
            if (fresh && SynonymFile::parse(field->text(), parsed, &reason) &&
                (adding ? file.add(field->text(), excluded->isChecked(), &reason) :
                          file.edit(row, field->text(), excluded->isChecked(), &reason))) {
                search_->clear();
                updateList({parsed, excluded->isChecked()});
                showError({});
                form->accept();
                owner_.changed();
                return;
            }
            error->setText(reason);
            error->show();
            field->setFocus();
        });
        connect(form, &QDialog::finished, this, [this] { form_.clear(); list_->setFocus(); });
        form->resize(qMin(530, int(screen()->availableGeometry().width() * 0.85)), form->sizeHint().height());
        showWindow(form);
        field->setFocus();
        field->selectAll();
    }

    void confirm(const QString &text, const QString &accept, std::function<void()> action)
    {
        if (question_) { showWindow(question_); return; }
        auto *box = new QMessageBox(QMessageBox::Question, name_, text, QMessageBox::Cancel, this);
        question_ = box;
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setTextFormat(Qt::PlainText);
        auto *yes = box->addButton(accept, QMessageBox::DestructiveRole);
        box->setDefaultButton(QMessageBox::Cancel);
        box->setWindowModality(Qt::NonModal);
        connect(box, &QMessageBox::finished, this, [this, box, yes, action = std::move(action)] {
            question_.clear();
            if (box->clickedButton() == yes) action();
            list_->setFocus();
        });
        showWindow(box);
    }

    void askDelete()
    {
        const int row = currentRow();
        if (row < 0 || row >= file.groups().size()) return;
        const auto original = file.groups().at(row);
        confirm(QStringLiteral("Delete the synonym group %1?").arg(SynonymFile::format(original.aliases)), QStringLiteral("&Delete"),
                [this, row, original] {
            if (!(file.groups().value(row) == original)) {
                showError(QStringLiteral("The selected group changed. Select it again."));
                return;
            }
            QString error;
            if (file.remove(row, &error)) {
                updateList();
                owner_.changed();
            }
            showError(error);
        });
    }

    void reloadList()
    {
        if (file.manuscriptPath().isEmpty()) return;
        auto reload = [this] {
            SynonymFile fresh(QStringLiteral(".") + id_);
            QString error;
            if (fresh.openManuscript(file.manuscriptPath(), &error)) {
                file = std::move(fresh);
                if (form_) form_->close();
                updateList();
                owner_.changed();
            }
            showError(error);
        };
        if (file.hasPending())
            confirm(QStringLiteral("Replace the pending list with the version on disk?"), QStringLiteral("&Reload"), reload);
        else
            reload();
    }

    SynonymPanel &owner_;
    QString name_, id_;
    GroupModel *model_;
    QListView *list_;
    PanelSearch *search_;
    QLabel *notice_;
    QPointer<QDialog> form_;
    QPointer<QMessageBox> question_;
};

SynonymPanel::SynonymPanel(QTextDocument *document, QWidget *parent)
    : QWidget(parent), document_(document), matcher_(new SynonymMatcher(document, this)),
      characters_(new Section(*this, QStringLiteral("Characters"), QStringLiteral(".characters"), QStringLiteral("Ctrl+Shift+1"))),
      recurring_(new Section(*this, QStringLiteral("Recurring"), QStringLiteral(".recurring"), QStringLiteral("Ctrl+Shift+2")))
{
    setObjectName(QStringLiteral("synonymPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(6);
    auto *split = new QSplitter(Qt::Vertical, this);
    split->setObjectName(QStringLiteral("synonymSplitter"));
    split->setAccessibleName(tr("Synonym panels"));
    split->setChildrenCollapsible(false);
    split->addWidget(characters_);
    split->addWidget(recurring_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    layout->addWidget(split);
    setAccessibleName(tr("Synonym lists"));
    setMinimumWidth(0);
}

void SynonymPanel::focusCharacters() { characters_->show(); characters_->focusSearch(); }
void SynonymPanel::focusRecurring() { recurring_->show(); recurring_->focusSearch(); }

void SynonymPanel::showOccurrences(int group)
{
    if (!document_) return;
    if (!results_) {
        results_ = new SynonymResults(document_, matcher_, this);
        results_->jumpToMatch = [this](int start, int end) {
            if (jumpToMatch) jumpToMatch(start, end);
        };
    }
    results_->showGroup(group);
}

void SynonymPanel::rebuildMatcher()
{
    matcher_->setGroups(characters_->file.groups(), recurring_->file.groups(), genres_);
}

void SynonymPanel::setGenres(const QStringList &genres)
{
    if (genres_ == genres) return;
    genres_ = genres;
    rebuildMatcher();
    if (results_) results_->refreshGroup();
    if (onChanged) onChanged();
}

bool SynonymPanel::showGenreOccurrences(const QString &genre)
{
    const int row = genres_.indexOf(genre);
    if (row < 0) return false;
    showOccurrences(int(characters_->file.groups().size() + recurring_->file.groups().size()) + row);
    return true;
}

void SynonymPanel::changed()
{
    if (results_) results_->reset();
    rebuildMatcher();
    if (onChanged) onChanged();
}

bool SynonymPanel::openManuscript(const QString &path, QString *error)
{
    QStringList errors;
    for (auto *section : {characters_, recurring_}) {
        section->clearView();
        QString reason;
        if (!section->file.openManuscript(path, &reason)) errors.append(reason);
        section->updateList();
    }
    changed();
    return report(errors, error);
}

bool SynonymPanel::saveAs(const QString &path, QString *error)
{
    QStringList errors;
    for (auto *section : {characters_, recurring_}) {
        QString reason;
        if (!section->file.saveAs(path, &reason)) errors.append(reason);
        section->updateList();
    }
    changed();
    return report(errors, error);
}

bool SynonymPanel::flush(QString *error)
{
    if (!hasPending()) return true;
    QStringList errors;
    for (auto *section : {characters_, recurring_}) {
        QString reason;
        if (!section->file.flush(&reason)) errors.append(reason);
    }
    if (onChanged) onChanged();
    return report(errors, error);
}

bool SynonymPanel::hasPending() const
{
    return characters_->file.hasPending() || recurring_->file.hasPending();
}

QStringList SynonymPanel::allAliases() const
{
    QStringList aliases = genres_;
    for (auto *section : {characters_, recurring_})
        for (const auto &group : section->file.groups())
            aliases.append(group.aliases);
    return aliases;
}

void SynonymPanel::showCharacters(bool visible) { characters_->setVisible(visible); }
