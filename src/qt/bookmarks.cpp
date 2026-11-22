#include "bookmarks.h"
#include "commandicons.h"
#include "layoutcolumns.h"
#include "bookmarkfile.h"
#include "promptlineedit.h"

#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtGui/QKeyEvent>
#include <QtGui/QScreen>
#include <QtGui/QTextDocument>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {

enum ItemRole { CategoryRole = Qt::UserRole, NameRole, LineRole };

QLabel *messageLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->hide();
    return label;
}

void message(QLabel *label, const QString &text)
{
    label->setText(text);
    label->setVisible(!text.isEmpty());
}

void showDialog(QDialog *dialog)
{
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void identify(QTreeWidgetItem *item, const QString &category, const QString &name = {}, int line = 0)
{
    item->setData(0, CategoryRole, category);
    item->setData(0, NameRole, name);
    item->setData(0, LineRole, line);
}

QString displayTitle(int line, int lineCount, const std::function<QString(int)> &titleAtLine)
{
    return line <= lineCount ? titleAtLine(line) : QStringLiteral("Line outside the manuscript");
}

} // namespace

class Bookmarks::Dialogs final : public QObject {
public:
    Dialogs(Bookmarks &bookmarks, QWidget *parent, std::function<int()> current,
            QTextDocument *manuscript, std::function<QString(int)> title,
            std::function<void(int)> jump)
        : control(bookmarks), file(manuscript), document(manuscript), owner(parent),
          currentLine(std::move(current)), titleAtLine(std::move(title)), jumpToLine(std::move(jump))
    {
        refreshTimer.setSingleShot(true);
        refreshTimer.setInterval(100);
        connect(&refreshTimer, &QTimer::timeout, this, [this] { refreshTitles(); });
        connect(document, &QTextDocument::undoCommandAdded, this, [this] { file.noteUndoCommand(); });
        connect(document, &QTextDocument::contentsChange, this, [this](int position, int removed, int added) {
            if (!removed && !added) return;
            const bool wasPending = file.hasPending();
            auto *shown = form && form->isVisible() && kind == EditKind::Add ? &formTarget : nullptr;
            if (file.trackEdit(position, added, shown)) titles.clear();
            if (shown) updateTargetCaption(shown->line);
            if (!wasPending && file.hasPending()) changed();
            control.refresh();
        });
    }

    ~Dialogs() override
    {
        delete confirm.data();
        delete form.data();
        delete manager.data();
    }

    enum class EditKind { Add, RenameBookmark, RenameCategory };

    Bookmarks &control;
    BookmarkFile file;
    QTextDocument *document;
    QPointer<QWidget> owner;
    std::function<int()> currentLine;
    std::function<QString(int)> titleAtLine;
    std::function<void(int)> jumpToLine;

    QPointer<QDialog> manager;
    QLineEdit *filter = nullptr;
    QComboBox *mode = nullptr;
    QTreeWidget *items = nullptr;
    QLabel *notice = nullptr;
    QPushButton *go = nullptr;
    QPushButton *rename = nullptr;
    QPushButton *remove = nullptr;
    QPushButton *reload = nullptr;
    QPushButton *retry = nullptr;
    QTimer refreshTimer;
    QHash<int, QString> titles;

    QPointer<QDialog> form;
    QComboBox *categoryField = nullptr;
    QLineEdit *nameField = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *target = nullptr;
    QLabel *formError = nullptr;
    QPushButton *apply = nullptr;
    EditKind kind = EditKind::Add;
    QString originalCategory;
    QString originalName;
    LineBookmark formTarget;
    QPointer<QMessageBox> confirm;
    quint64 revision = 0;

    void changed()
    {
        ++revision;
        if (control.onChanged)
            control.onChanged();
    }

    void prepareManager()
    {
        manager = new QDialog(owner);
        manager->setObjectName(QStringLiteral("bookmarkWindow"));
        manager->setWindowTitle(QStringLiteral("Bookmarks"));
        manager->resize(630, 420);
        auto *layout = new QVBoxLayout(manager);
        auto *filters = new QHBoxLayout;
        auto *filterLabel = new QLabel(QStringLiteral("&Filter"), manager);
        filter = new PromptLineEdit(manager);
        filter->setObjectName(QStringLiteral("bookmarkFilter"));
        CommandIcons::addSearchClearButton(filter);
        filterLabel->setBuddy(filter);
        auto *viewLabel = new QLabel(QStringLiteral("&View"), manager);
        mode = new QComboBox(manager);
        mode->setObjectName(QStringLiteral("bookmarkViewMode"));
        mode->addItems({QStringLiteral("Tree"), QStringLiteral("List")});
        viewLabel->setBuddy(mode);
        filters->addWidget(filterLabel);
        filters->addWidget(filter, 1);
        filters->addWidget(viewLabel);
        filters->addWidget(mode);
        layout->addLayout(filters);

        items = new QTreeWidget(manager);
        items->setObjectName(QStringLiteral("bookmarkResults"));
        items->setAccessibleName(QStringLiteral("Bookmarks"));
        items->setColumnCount(4);
        items->setSelectionMode(QAbstractItemView::SingleSelection);
        items->setEditTriggers(QAbstractItemView::NoEditTriggers);
        items->setUniformRowHeights(true);
        items->setExpandsOnDoubleClick(false);
        items->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        items->header()->resizeSection(0, 175);
        items->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        items->header()->setStretchLastSection(true);
        layout->addWidget(items, 1);
        notice = messageLabel(manager);
        notice->setObjectName(QStringLiteral("bookmarkNotice"));
        layout->addWidget(notice);

        auto *buttons = new QDialogButtonBox(manager);
        go = buttons->addButton(QStringLiteral("&Go to"), QDialogButtonBox::ActionRole);
        rename = buttons->addButton(QStringLiteral("&Rename…"), QDialogButtonBox::ActionRole);
        remove = buttons->addButton(QStringLiteral("&Delete…"), QDialogButtonBox::ActionRole);
        reload = buttons->addButton(QStringLiteral("Re&load"), QDialogButtonBox::ActionRole);
        retry = buttons->addButton(QStringLiteral("Retry &Save"), QDialogButtonBox::ActionRole);
        auto *close = buttons->addButton(QDialogButtonBox::Close);
        for (QAbstractButton *button : buttons->buttons())
            static_cast<QPushButton *>(button)->setAutoDefault(false);
        layout->addWidget(buttons);

        connect(filter, &QLineEdit::textChanged, this, [this] { fill(true); });
        connect(mode, &QComboBox::currentIndexChanged, this, [this] { fill(true); });
        connect(items, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
        connect(items, &QTreeWidget::itemActivated, this, [this] { activate(); });
        connect(go, &QPushButton::clicked, this, [this] { activate(); });
        connect(rename, &QPushButton::clicked, this, [this] { editSelection(); });
        connect(remove, &QPushButton::clicked, this, [this] { deleteSelection(); });
        connect(reload, &QPushButton::clicked, this, [this] { reloadFile(); });
        connect(retry, &QPushButton::clicked, this, [this] {
            QString error;
            if (control.flush(&error))
                message(notice, QStringLiteral("Bookmarks saved."));
            else
                message(notice, error);
        });
        connect(close, &QPushButton::clicked, manager, &QDialog::reject);
        auto *find = new QAction(manager);
        find->setShortcut(QKeySequence::Find);
#ifdef Q_OS_MACOS
        find->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_F), QKeySequence(Qt::META | Qt::Key_F)});
#endif
        manager->addAction(find);
        connect(find, &QAction::triggered, this, [this] { filter->setFocus(); filter->selectAll(); });
        filter->installEventFilter(this);
        items->installEventFilter(this);
    }

    bool eventFilter(QObject *source, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() != Qt::NoModifier && key->modifiers() != Qt::KeypadModifier)
            return false;
        if (form && form->isVisible() && (source == nameField || source == categoryField ||
                                          source == categoryField->lineEdit())) {
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                acceptEdit();
                return true;
            }
            return false;
        }
        if (source != filter && source != items)
            return false;
        switch (key->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            activate();
            return true;
        case Qt::Key_Down:
        case Qt::Key_Up:
            if (source == filter) {
                items->setFocus();
                return true;
            }
            break;
        case Qt::Key_F2:
            if (source == items) { editSelection(); return true; }
            break;
        case Qt::Key_Delete:
#ifdef Q_OS_MACOS
        case Qt::Key_Backspace:
#endif
            if (source == items) { deleteSelection(); return true; }
            break;
        case Qt::Key_F5:
            reloadFile();
            return true;
        }
        return false;
    }

    void updateButtons()
    {
        QTreeWidgetItem *item = items->currentItem();
        go->setEnabled(item && item->data(0, LineRole).toInt() > 0);
        rename->setEnabled(item);
        remove->setEnabled(item);
        reload->setEnabled(!file.manuscriptPath().isEmpty());
        retry->setVisible(file.hasPending() && !file.manuscriptPath().isEmpty());
    }

    void refreshTitles()
    {
        if (!manager || !manager->isVisible())
            return;
        QHash<int, QString> current;
        const int count = file.lineCount();
        for (const LineBookmark &entry : file.entries())
            if (!current.contains(entry.line))
                current.insert(entry.line, displayTitle(entry.line, count, titleAtLine));
        if (current != titles)
            fill(false);
    }

    struct BookmarkRow {
        const LineBookmark *bookmark;
        QString title;
    };

    void fill(bool newFilter)
    {
        const bool tree = mode->currentIndex() == 0;
        const QStringList terms = filter->text().simplified().split(u' ', Qt::SkipEmptyParts);
        const QTreeWidgetItem *selected = items->currentItem();
        const QString oldCategory = selected ? selected->data(0, CategoryRole).toString() : QString();
        const QString oldName = selected ? selected->data(0, NameRole).toString() : QString();
        QSet<QString> collapsed;
        if (!newFilter && terms.isEmpty()) {
            for (int i = 0; i < items->topLevelItemCount(); ++i) {
                auto *parent = items->topLevelItem(i);
                if (parent->childCount() && !parent->isExpanded())
                    collapsed.insert(parent->data(0, CategoryRole).toString());
            }
        }
        items->setUpdatesEnabled(false);
        items->clear();
        items->setRootIsDecorated(tree);
        LayoutColumns::setOptionalColumn(items, 1, !tree);
        items->setHeaderLabels({tree ? QStringLiteral("Category / Bookmark") : QStringLiteral("Category"),
                                QStringLiteral("Name"), QStringLiteral("Line"), QStringLiteral("Manuscript text")});
        QMap<QString, QList<BookmarkRow>> rows;
        QHash<QString, int> totalInCategory;
        const int count = file.lineCount();
        titles.clear();
        for (const LineBookmark &entry : file.entries()) {
            ++totalInCategory[entry.category];
            if (!titles.contains(entry.line))
                titles.insert(entry.line, displayTitle(entry.line, count, titleAtLine));
            const QString title = titles.value(entry.line);
            const QString searchable = entry.category + u' ' + entry.name + u' '
                                       + QString::number(entry.line) + u' ' + title;
            if (std::all_of(terms.begin(), terms.end(), [&](const QString &term) {
                    return searchable.contains(term, Qt::CaseInsensitive);
                }))
                rows[entry.category].append({&entry, title});
        }

        QTreeWidgetItem *first = nullptr;
        QTreeWidgetItem *restore = nullptr;
        for (const QString &category : file.categories()) {
            const auto found = rows.constFind(category);
            if (found == rows.constEnd())
                continue;
            QTreeWidgetItem *group = nullptr;
            if (tree && totalInCategory.value(category) > 1) {
                group = new QTreeWidgetItem(items, {category});
                identify(group, category);
                if (category == oldCategory && oldName.isEmpty() && !newFilter)
                    restore = group;
            }
            for (const BookmarkRow &row : *found) {
                const LineBookmark &entry = *row.bookmark;
                const QStringList columns{group ? entry.name : category, entry.name,
                                          QString::number(entry.line), row.title};
                auto *leaf = group ? new QTreeWidgetItem(group, columns)
                                   : new QTreeWidgetItem(items, columns);
                identify(leaf, category, entry.name, entry.line);
                leaf->setToolTip(0, category + QStringLiteral(" / ") + entry.name);
                if (!first)
                    first = leaf;
                if (category == oldCategory && entry.name == oldName)
                    restore = leaf;
            }
            if (group)
                group->setExpanded(!collapsed.contains(category));
        }
        if (restore || first)
            items->setCurrentItem(restore ? restore : first);
        items->setUpdatesEnabled(true);
        updateButtons();
        if (!first)
            message(notice, file.entries().isEmpty()
                    ? QStringLiteral("No bookmarks. Ctrl+B in the manuscript adds one.")
                    : QStringLiteral("No matching bookmarks."));
        else if (newFilter)
            message(notice, {});
    }

    void showManager()
    {
        if (!manager)
            prepareManager();
        const QString name = QFileInfo(file.manuscriptPath()).fileName();
        manager->setWindowTitle(name.isEmpty() ? QStringLiteral("Bookmarks — Untitled")
                                                : QStringLiteral("Bookmarks — ") + name);
        fill(true);
        showDialog(manager);
        filter->setFocus();
        filter->selectAll();
    }

    void activate()
    {
        if (!manager || !manager->isVisible() || !items->currentItem())
            return;
        auto *item = items->currentItem();
        const auto category = item->data(0, CategoryRole).toString();
        const auto name = item->data(0, NameRole).toString();
        if (name.isEmpty()) {
            item->setExpanded(!item->isExpanded());
            items->setFocus();
            return;
        }
        // The displayed rows refresh on a timer. Enter must always use the
        // live anchor when the manuscript changed just before activation.
        const auto &entries = file.entries();
        const auto found = std::find_if(entries.cbegin(), entries.cend(), [&](const LineBookmark &entry) {
            return entry.category == category && entry.name == name;
        });
        if (found == entries.cend()) return;
        const int line = found->line;
        manager->hide();
        owner->raise();
        owner->activateWindow();
        jumpToLine(line);
    }

    void prepareForm()
    {
        form = new QDialog(owner);
        form->setObjectName(QStringLiteral("bookmarkEditWindow"));
        auto *layout = new QVBoxLayout(form);
        target = messageLabel(form);
        layout->addWidget(target);
        auto *fields = new QFormLayout;
        fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
        categoryField = new QComboBox(form);
        categoryField->setObjectName(QStringLiteral("bookmarkCategory"));
        categoryField->setEditable(true);
        categoryField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        categoryField->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        categoryField->setMinimumContentsLength(1);
        categoryField->setInsertPolicy(QComboBox::NoInsert);
        categoryField->setCompleter(nullptr);
        fields->addRow(QStringLiteral("&Category"), categoryField);
        nameField = new PromptLineEdit(form);
        nameField->setObjectName(QStringLiteral("bookmarkName"));
        nameLabel = new QLabel(QStringLiteral("&Name"), form);
        nameLabel->setBuddy(nameField);
        fields->addRow(nameLabel, nameField);
        layout->addLayout(fields);
        formError = messageLabel(form);
        formError->setObjectName(QStringLiteral("bookmarkEditNotice"));
        layout->addWidget(formError);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, form);
        apply = buttons->addButton(QStringLiteral("&Add"), QDialogButtonBox::AcceptRole);
        apply->setDefault(true);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { acceptEdit(); });
        connect(buttons, &QDialogButtonBox::rejected, form, &QDialog::reject);
        connect(categoryField, &QComboBox::editTextChanged, this, [this](const QString &value) {
            if (kind == EditKind::Add)
                nameField->setPlaceholderText(file.nextName(value));
        });
        categoryField->installEventFilter(this);
        categoryField->lineEdit()->installEventFilter(this);
        nameField->installEventFilter(this);
    }

    void edit(EditKind editKind, QString category, QString name, int line)
    {
        const bool firstUse = !form;
        if (firstUse)
            prepareForm();
        kind = editKind;
        originalCategory = category;
        originalName = name;
        formTarget = {};
        formTarget.line = line;
        if (line > 0 && line <= file.lineCount()) file.attach(formTarget, line);
        const bool add = kind == EditKind::Add;
        const bool grouped = kind == EditKind::RenameCategory;
        form->setWindowTitle(add ? QStringLiteral("Add Bookmark")
                                 : grouped ? QStringLiteral("Rename Category") : QStringLiteral("Edit Bookmark"));
        apply->setText(add ? QStringLiteral("&Add") : QStringLiteral("&Save"));
        categoryField->clear();
        categoryField->addItems(file.categories());
        categoryField->setEditText(category);
        nameField->setText(name);
        nameField->setPlaceholderText(add ? file.nextName(category) : QString());
        nameField->setVisible(!grouped);
        nameLabel->setVisible(!grouped);
        updateTargetCaption(grouped ? 0 : line);
        message(formError, {});
        const int available = qMin(owner->width(), owner->screen()->availableGeometry().width() * 9 / 10);
        if (firstUse || form->width() > available) {
            const int preferred = qMax(640, 68 * categoryField->fontMetrics().averageCharWidth());
            form->resize(qMin(preferred, available), form->sizeHint().height());
        }
        showDialog(form);
        QLineEdit *focus = kind == EditKind::RenameBookmark ? nameField : categoryField->lineEdit();
        focus->setFocus();
        focus->selectAll();
    }

    void updateTargetCaption(int line)
    {
        if (line <= 0) { message(target, {}); return; }
        QString caption = QStringLiteral("Line %1").arg(line);
        const auto title = displayTitle(line, file.lineCount(), titleAtLine);
        if (!title.isEmpty()) caption += QStringLiteral(" — ") + title;
        message(target, caption);
    }

    void editSelection()
    {
        const auto *selected = items->currentItem();
        if (!selected)
            return;
        const int line = selected->data(0, LineRole).toInt();
        edit(line ? EditKind::RenameBookmark : EditKind::RenameCategory,
             selected->data(0, CategoryRole).toString(), selected->data(0, NameRole).toString(), line);
    }

    void acceptEdit()
    {
        if (!form || !form->isVisible())
            return;
        const QString category = categoryField->currentText();
        const QString name = nameField->text();
        QString error;
        bool ok;
        switch (kind) {
        case EditKind::Add:
            if (formTarget.anchor.isNull()) {
                message(formError, QStringLiteral("Choose an existing line in the manuscript and press Ctrl+B."));
                return;
            }
            ok = file.add(category, name, formTarget.line, &error);
            break;
        case EditKind::RenameBookmark:
            ok = file.renameBookmark(originalCategory, originalName, category, name, &error);
            break;
        case EditKind::RenameCategory:
            ok = file.renameCategory(originalCategory, category, &error);
            break;
        default:
            return;
        }
        if (!ok) {
            message(formError, error);
            return;
        }
        form->hide();
        if (manager && manager->isVisible()) {
            fill(true);
            manager->raise();
            manager->activateWindow();
            items->setFocus();
        } else if (owner) {
            owner->raise();
            owner->activateWindow();
        }
        changed();
    }

    void ask(const QString &text, const QString &accept, std::function<void()> action)
    {
        if (confirm) {
            showDialog(confirm);
            return;
        }
        auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Bookmarks"), text,
                                    QMessageBox::Cancel, manager);
        confirm = box;
        box->setTextFormat(Qt::PlainText);
        box->setAttribute(Qt::WA_DeleteOnClose);
        auto *button = box->addButton(accept, QMessageBox::DestructiveRole);
        box->setDefaultButton(QMessageBox::Cancel);
        const quint64 requestedAt = revision;
        connect(box, &QMessageBox::finished, this, [this, box, button, requestedAt, action = std::move(action)] {
            if (box->clickedButton() != button)
                return;
            if (revision == requestedAt)
                action();
            else
                message(notice, QStringLiteral("Bookmarks changed. Select the entry again."));
        });
        box->setWindowModality(Qt::NonModal);
        showDialog(box);
    }

    void deleteSelection()
    {
        const auto *selected = items->currentItem();
        if (!selected)
            return;
        const QString category = selected->data(0, CategoryRole).toString();
        const QString name = selected->data(0, NameRole).toString();
        const QString text = name.isEmpty()
            ? QStringLiteral("Delete all bookmarks in %1?").arg(category)
            : QStringLiteral("Delete bookmark %1 / %2?").arg(category, name);
        ask(text, QStringLiteral("Delete"), [this, category, name] {
            QString error;
            if (file.remove(category, name, &error)) {
                fill(true);
                changed();
            } else {
                message(notice, error);
            }
        });
    }

    void reloadFile()
    {
        if (file.manuscriptPath().isEmpty())
            return;
        const auto reloadFromDisk = [this] {
            BookmarkFile fresh(document);
            QString error;
            if (!fresh.openManuscript(file.manuscriptPath(), &error)) {
                message(notice, error);
                return;
            }
            if (form)
                form->hide();
            file = std::move(fresh);
            fill(true);
            message(notice, QStringLiteral("Bookmarks reloaded."));
            changed();
        };
        if (file.hasPending())
            ask(QStringLiteral("Replace the pending bookmarks with the bookmarks on disk?"),
                QStringLiteral("Reload from Disk"), reloadFromDisk);
        else
            reloadFromDisk();
    }

    void changedManuscript()
    {
        refreshTimer.stop();
        if (confirm)
            confirm->close();
        if (form)
            form->hide();
        if (manager) {
            filter->clear();
            if (manager->isVisible())
                showManager();
        }
    }
};

Bookmarks::Bookmarks(QWidget *owner, std::function<int()> currentLine,
                     QTextDocument *document, std::function<QString(int)> titleAtLine,
                     std::function<void(int)> jumpToLine)
    : QObject(owner), dialogs_(std::make_unique<Dialogs>(*this, owner, std::move(currentLine), document,
                                         std::move(titleAtLine), std::move(jumpToLine)))
{
}

Bookmarks::~Bookmarks() = default;

bool Bookmarks::openManuscript(const QString &path, QString *error)
{
    const bool ok = dialogs_->file.openManuscript(path, error);
    dialogs_->changedManuscript();
    return ok;
}

bool Bookmarks::saveAs(const QString &path, QString *error)
{
    const bool ok = dialogs_->file.saveAs(path, error);
    dialogs_->changedManuscript();
    dialogs_->changed();
    return ok;
}

bool Bookmarks::flush(QString *error)
{
    const bool wasPending = hasPending();
    const bool ok = dialogs_->file.flush(error);
    if (ok && wasPending) {
        if (dialogs_->manager)
            dialogs_->updateButtons();
        dialogs_->changed();
    }
    return ok;
}

bool Bookmarks::hasPending() const
{
    return dialogs_->file.hasPending();
}

void Bookmarks::captureForSave()
{
    dialogs_->file.captureForSave();
}

bool Bookmarks::publishSave(QString *error)
{
    const bool wasPending = hasPending();
    const bool ok = dialogs_->file.publishSave(error);
    if (wasPending != hasPending()) {
        if (dialogs_->manager) dialogs_->updateButtons();
        dialogs_->changed();
    }
    return ok;
}

void Bookmarks::addCurrent()
{
    dialogs_->edit(Dialogs::EditKind::Add, dialogs_->file.lastCategory(), {}, dialogs_->currentLine());
}

void Bookmarks::showWindow()
{
    dialogs_->showManager();
}

void Bookmarks::refresh()
{
    if (dialogs_->manager && dialogs_->manager->isVisible() && !dialogs_->refreshTimer.isActive())
        dialogs_->refreshTimer.start();
}
