#include "symbolpicker.h"

#include "manuscripteditor.h"
#include "symbolcatalog.h"
#include "textkeys.h"

#include <QAbstractListModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const char favoriteKey[] = "symbols/favorites";
const char recentKey[] = "symbols/recent";
enum Scope { All, Favorites, Recent, FirstTheme };

QLabel *plainLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    return label;
}
}

class SymbolListModel final : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(rows_.size());
    }
    const WriterSymbol *symbol(const QModelIndex &index) const
    {
        if (!index.isValid() || index.row() >= rows_.size()) return nullptr;
        return &SymbolCatalog::instance().at(rows_.at(index.row()));
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        const auto *entry = symbol(index);
        if (!entry) return {};
        switch (role) {
        case Qt::DisplayRole: return entry->sample();
        case Qt::AccessibleTextRole: return entry->name;
        case Qt::AccessibleDescriptionRole: return entry->theme + ", " + entry->code;
        case Qt::ToolTipRole: return entry->name + " (" + entry->code + ")";
        case Qt::TextAlignmentRole: return int(Qt::AlignCenter);
        case Qt::UserRole: return entry->text;
        default: return {};
        }
    }
    void setRows(QList<int> rows)
    {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
    }
    QModelIndex byCode(const QString &code) const
    {
        if (!code.isEmpty())
            for (int i = 0; i < rows_.size(); ++i)
                if (SymbolCatalog::instance().at(rows_.at(i)).code == code) return index(i);
        return index(0);
    }

private:
    QList<int> rows_;
};

SymbolPicker::SymbolPicker(ManuscriptEditor *editor, QSettings &settings, QWidget *parent)
    : QDialog(parent), editor_(editor), settings_(settings)
{
    setObjectName("symbolPicker");
    setWindowTitle(tr("Insert Symbol"));
    setModal(false);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName("symbolSearch");
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(tr("Name, description, theme, symbol, or U+ code"));
    search_->setToolTip(tr("Search symbols (Alt+S or Ctrl+F)"));
    form->addRow(tr("Search"), search_);
    theme_ = new QComboBox(this);
    theme_->setObjectName("symbolTheme");
    theme_->addItems({tr("All symbols"), tr("Favorites"), tr("Recently used")});
    theme_->addItems(SymbolCatalog::instance().themes());
    theme_->setMaxVisibleItems(theme_->count());
    theme_->setToolTip(tr("Choose a theme (Alt+T or Ctrl+T)"));
    form->addRow(tr("Theme"), theme_);
    layout->addLayout(form);

    model_ = new SymbolListModel(this);
    grid_ = new QListView(this);
    grid_->setObjectName("symbolGrid");
    grid_->setAccessibleName(tr("Matching symbols"));
    grid_->setModel(model_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setMovement(QListView::Static);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setUniformItemSizes(true);
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setWordWrap(false);
    grid_->setTextElideMode(Qt::ElideNone);
    grid_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(grid_, 1);

    auto *sampleRow = new QHBoxLayout;
    preview_ = plainLabel(this);
    preview_->setObjectName("symbolGlyph");
    preview_->setAlignment(Qt::AlignCenter);
    auto *metadata = new QVBoxLayout;
    name_ = plainLabel(this);
    name_->setObjectName("symbolName");
    auto nameFont = name_->font();
    nameFont.setBold(true);
    name_->setFont(nameFont);
    details_ = plainLabel(this);
    details_->setObjectName("symbolDetails");
    details_->setWordWrap(true);
    metadata->addWidget(name_);
    metadata->addWidget(details_);
    sampleRow->addWidget(preview_);
    sampleRow->addLayout(metadata, 1);
    layout->addLayout(sampleRow);

    status_ = plainLabel(this);
    status_->setObjectName("symbolCount");
    layout->addWidget(status_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    favorite_ = buttons->addButton(tr("Add to &Favorites"), QDialogButtonBox::ActionRole);
    favorite_->setObjectName("symbolFavorite");
    favorite_->setToolTip(tr("Toggle favorite (Ctrl+D)"));
    insert_ = buttons->addButton(tr("&Insert"), QDialogButtonBox::AcceptRole);
    insert_->setObjectName("symbolInsert");
    insert_->setToolTip(tr("Enter inserts and closes. Ctrl+Enter keeps the picker open."));
    for (auto *button : buttons->buttons())
        if (auto *push = qobject_cast<QPushButton *>(button)) push->setAutoDefault(false);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(favorite_, &QPushButton::clicked, this, &SymbolPicker::toggleFavorite);
    connect(insert_, &QPushButton::clicked, this, [this] { insertCurrent(false); });
    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(theme_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(grid_, &QListView::doubleClicked, this, [this] { insertCurrent(false); });
    connect(grid_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updatePreview(); });

    auto *favoriteShortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
    connect(favoriteShortcut, &QShortcut::activated, this, &SymbolPicker::toggleFavorite);
    auto *keepOpen = new QShortcut(QKeySequence("Ctrl+Return"), this);
    keepOpen->setKeys({QKeySequence("Ctrl+Return"), QKeySequence("Ctrl+Enter")});
    connect(keepOpen, &QShortcut::activated, this, [this] { insertCurrent(true); });
    // The combo's popup is a separate window. Receive its focused child's
    // keystrokes even when the platform suspends dialog-level shortcuts.
    for (auto *child : findChildren<QWidget *>()) child->installEventFilter(this);
    theme_->view()->installEventFilter(this);
    theme_->view()->window()->installEventFilter(this);
    setTabOrder(search_, theme_);
    setTabOrder(theme_, grid_);
    setFocusProxy(search_);
    if (screen()) resize(QSize(660, 500).boundedTo(screen()->availableGeometry().size() * 0.85));
}

void SymbolPicker::openPicker()
{
    settings_.sync();
    theme_->setCurrentIndex(All);
    search_->clear();
    const auto original = editor_->font();
    manuscriptFont_ = QRawFont::fromFont(original);
    QFont glyphFont = original;
    glyphFont.setPointSizeF(std::clamp(QFontInfo(original).pointSizeF() * 1.55, 18.0, 29.0));
    grid_->setFont(glyphFont);
    const int cell = std::max(48, qRound(grid_->fontMetrics().height() * 1.4));
    grid_->setGridSize(QSize(cell, cell));
    glyphFont.setPointSizeF(glyphFont.pointSizeF() * 1.45);
    preview_->setFont(glyphFont);
    preview_->setFixedWidth(std::max(cell + 10, preview_->fontMetrics().height() * 2));
    refresh();
    show();
    raise();
    activateWindow();
    search_->setFocus(Qt::ShortcutFocusReason);
}

const WriterSymbol *SymbolPicker::current() const
{
    return model_->symbol(grid_->currentIndex());
}

void SymbolPicker::refresh(const QString &keepCode)
{
    const int scope = theme_->currentIndex();
    const auto &catalog = SymbolCatalog::instance();
    const QStringList stored = settings_.value(scope == Favorites ? favoriteKey : recentKey).toStringList();
    model_->setRows(catalog.search(search_->text(), scope >= FirstTheme ? theme_->currentText() : QString(),
                                  scope == Favorites || scope == Recent ? &stored : nullptr));
    grid_->setCurrentIndex(model_->byCode(keepCode));
    if (grid_->currentIndex().isValid()) grid_->scrollTo(grid_->currentIndex());
    const auto size = model_->rowCount();
    if (size == 0 && search_->text().isEmpty() && scope == Favorites)
        status_->setText(tr("Choose a symbol in All symbols and press Ctrl+D to add a favorite."));
    else if (size == 0 && search_->text().isEmpty() && scope == Recent)
        status_->setText(tr("Symbols appear here after insertion."));
    else if (size == 0)
        status_->setText(tr("No symbols match this search."));
    else
        status_->setText(size == 1 ? tr("1 symbol") : tr("%1 symbols").arg(size));
    updatePreview();
}

void SymbolPicker::updatePreview()
{
    const auto *entry = current();
    insert_->setEnabled(entry && !editor_->isReadOnly());
    favorite_->setEnabled(entry);
    const bool favorite = entry && settings_.value(favoriteKey).toStringList().contains(entry->code);
    favorite_->setText(favorite ? tr("Remove from &Favorites") : tr("Add to &Favorites"));
    if (!entry) {
        preview_->clear();
        name_->clear();
        details_->clear();
        return;
    }
    preview_->setText(entry->sample());
    preview_->setAccessibleName(entry->name);
    name_->setText(entry->name);
    QStringList details{entry->code};
    if (entry->name.compare(entry->officialName, Qt::CaseInsensitive) != 0)
        details.append(entry->officialName);
    details.append(entry->theme);
    if (entry->showSpacing) details.append(tr("Width shown between two bars"));
    else if (manuscriptFont_.isValid() && !manuscriptFont_.supportsCharacter(entry->text.toUcs4().first()))
        details.append(tr("The manuscript font needs a fallback for this symbol"));
    details_->setText(details.join(QStringLiteral(" · ")));
}

void SymbolPicker::insertCurrent(bool keepOpen)
{
    const auto *entry = current();
    if (!entry || editor_->isReadOnly()) return;
    editor_->insertLiteral(entry->text);
    auto recent = settings_.value(recentKey).toStringList();
    recent.removeAll(entry->code);
    recent.prepend(entry->code);
    while (recent.size() > 32) recent.removeLast();
    settings_.setValue(recentKey, recent);
    if (keepOpen) refresh(entry->code);
    else accept();
}

void SymbolPicker::toggleFavorite()
{
    const auto *entry = current();
    if (!entry) return;
    auto list = settings_.value(favoriteKey).toStringList();
    if (!list.removeAll(entry->code)) list.append(entry->code);
    settings_.setValue(favoriteKey, list);
    refresh(entry->code);
}

void SymbolPicker::focusSearch()
{
    if (theme_->view()->isVisible()) theme_->hidePopup();
    // Some styles restore activation to the combo after releasing the popup
    // grab. Reclaim the dialog before assigning the new input focus.
    activateWindow();
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
}

bool SymbolPicker::eventFilter(QObject *watched, QEvent *event)
{
    const bool overrideShortcut = event->type() == QEvent::ShortcutOverride;
    if (!overrideShortcut && event->type() != QEvent::KeyPress)
        return QDialog::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    const bool textTarget = watched == search_ || watched == grid_;
    const bool composed = textTarget && isComposedTextKey(*key);
    const bool searchKey = (mods == Qt::ControlModifier && key->key() == Qt::Key_F) ||
                           (mods == Qt::AltModifier && key->key() == Qt::Key_S);
    const bool themeKey = (mods == Qt::ControlModifier || mods == Qt::AltModifier) && key->key() == Qt::Key_T;
    if (overrideShortcut) {
        if (composed || searchKey || themeKey) {
            key->accept();
            return true;
        }
        return QDialog::eventFilter(watched, event);
    }
    if (composed) {
        // Option on macOS and AltGr elsewhere produce real text, including
        // with letters that also serve as mnemonics on other layouts.
        if (watched == grid_) search_->setFocus(Qt::ShortcutFocusReason);
        search_->insert(key->text());
        return true;
    }
    if (searchKey) { focusSearch(); return true; }
    if (themeKey) {
        if (!theme_->view()->isVisible()) {
            theme_->setFocus(Qt::ShortcutFocusReason);
            theme_->showPopup();
        }
        return true;
    }
    if (!textTarget) return QDialog::eventFilter(watched, event);
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        (mods == Qt::NoModifier || mods == Qt::ControlModifier)) {
        insertCurrent(mods == Qt::ControlModifier);
        return true;
    }
    if (key->key() == Qt::Key_D && mods == Qt::ControlModifier) {
        toggleFavorite();
        return true;
    }
    if (watched == search_ && key->key() == Qt::Key_Down && mods == Qt::NoModifier) {
        grid_->setFocus(Qt::TabFocusReason);
        return true;
    }
    if (watched == grid_) {
        if (key->key() == Qt::Key_Backspace && mods == Qt::NoModifier) {
            search_->setFocus();
            search_->backspace();
            return true;
        }
        if ((!mods || mods == Qt::ShiftModifier) &&
            !key->text().isEmpty() && key->text().front().isPrint()) {
            search_->setFocus();
            search_->insert(key->text());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SymbolPicker::done(int result)
{
    QDialog::done(result);
    editor_->window()->activateWindow();
    editor_->setFocus(Qt::OtherFocusReason);
}
