#include "navigation.h"
#include "layoutcolumns.h"
#include "editorcolors.h"
#include <QEvent>
#include "manuscripteditor.h"
#include "manuscriptnumbers.h"
#include "panelsearch.h"

#include <QCollator>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// The default information panel fits its text. A saved manual height takes
// precedence, including when a manuscript switches between writing modes.
class InformationPanel final : public QWidget {
public:
    explicit InformationPanel(QSplitter *split) : QWidget(split), split_(split) {}
protected:
    bool event(QEvent *event) override
    {
        const bool result = QWidget::event(event);
        if ((event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize ||
             event->type() == QEvent::Show) && !queued_ && isVisible()) {
            queued_ = true;
            QTimer::singleShot(0, this, [this] {
                queued_ = false;
                if (!isVisible() || !layout() || split_->property("layoutManualSizes").toBool()) return;
                const int me = split_->indexOf(this);
                auto sizes = split_->sizes();
                const int target = layout()->hasHeightForWidth()
                    ? layout()->totalHeightForWidth(width()) : layout()->totalSizeHint().height();
                const int difference = sizes[me] - target;
                if (!difference) return;
                for (int i = 0; i < sizes.size(); ++i) {
                    if (i == me || sizes[i] + difference <= 0 || split_->widget(i)->isHidden()) continue;
                    sizes[me] = target;
                    sizes[i] += difference;
                    split_->setSizes(sizes);
                    break;
                }
            });
        }
        return result;
    }
private:
    QSplitter *split_;
    bool queued_ = false;
};

class SidebarList final : public QTreeWidget {
public:
    explicit SidebarList(QWidget *parent) : QTreeWidget(parent)
    {
        setRootIsDecorated(false);
        setUniformRowHeights(true);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setMinimumSize(1, 1);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        header()->setStretchLastSection(true);
    }
protected:
    void keyboardSearch(const QString &) override {} // The labeled field owns text search.
};

class VisitItem final : public QTreeWidgetItem {
public:
    explicit VisitItem(const QTextCursor &at) : caret(at) {}
    QTextCursor caret;
};

QLabel *label(QWidget *parent, bool bold = false)
{
    auto *out = new QLabel(parent);
    out->setTextFormat(Qt::PlainText);
    out->setMinimumWidth(0);
    out->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont font = out->font();
    font.setBold(bold);
    out->setFont(font);
    return out;
}

void markNumber(QTreeWidgetItem *item, int column, bool warning, const QPalette &palette)
{
    QFont font = item->font(column);
    font.setBold(warning);
    item->setFont(column, font);
    item->setForeground(column, warning ? QBrush(EditorColors::warning(palette.base().color())) : QBrush());
}

QTextCursor insertionPoint(QTextCursor c)
{
    c.clearSelection();
    return c;
}

QString paragraphPreview(const QString &text)
{
    constexpr int limit = 96;
    const QStringView paragraph = QStringView(text).trimmed();
    if (paragraph.size() <= limit) return paragraph.toString();
    // A preview may end beside a supplementary Unicode character.
    const int length = paragraph.at(limit - 1).isHighSurrogate() ? limit - 1 : limit;
    return paragraph.left(length).trimmed().toString() + QChar(0x2026);
}

const char *orderPreference(bool generic)
{
    return generic ? "editor/title-sort-document-order" : "editor/genre-sort-by-number";
}
}

void JumpHistory::remember(QTextCursor cursor)
{
    cursor.clearSelection();
    if (cursor.isNull()) return;
    if (previous_.isEmpty() || previous_.last() != cursor) previous_.append(cursor);
    next_.clear();
}

QTextCursor JumpHistory::move(const QTextCursor &from, bool forward)
{
    auto &source = forward ? next_ : previous_;
    auto &reverse = forward ? previous_ : next_;
    const QTextCursor here = insertionPoint(from);
    while (!source.isEmpty()) {
        auto target = source.takeLast();
        if (target.isNull() || target.document() != here.document() || target == here) continue;
        if (reverse.isEmpty() || reverse.last() != here) reverse.append(here);
        return target;
    }
    return {};
}

void JumpHistory::clear() { previous_.clear(); next_.clear(); }

NavigationPanel::NavigationPanel(ManuscriptEditor *editor, QSettings *settings, QWidget *parent)
    : QWidget(parent), editor_(editor), settings_(settings)
{
    setObjectName("sidebar");
    setAccessibleName(tr("Navigation"));
    setMinimumSize(1, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    auto *split = new QSplitter(Qt::Vertical, this);
    split->setObjectName("navigationSplitter");
    split->setAccessibleName(tr("Navigation panels"));
    split->setChildrenCollapsible(false);
    auto *info = new InformationPanel(split);
    info->setObjectName("currentInfo");
    info->setAccessibleName(tr("Manuscript information"));
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    number_ = label(info, true);
    number_->setObjectName("currentCrumb");
    QFont numberFont = number_->font();
    numberFont.setPointSizeF(qMax(12.0, numberFont.pointSizeF() * 1.25));
    number_->setFont(numberFont);
    title_ = label(info);
    title_->setObjectName("crumbTitle");
    title_->setWordWrap(true);
    title_->setMaximumHeight(title_->fontMetrics().lineSpacing() * 4);
    words_ = label(info);
    words_->setObjectName("wordCount");
    today_ = label(info);
    today_->setObjectName("todayCount");
    for (auto *widget : {number_, title_, words_, today_}) infoLayout->addWidget(widget);
    split->addWidget(info);
    auto addList = [this, split](const QString &caption, const QString &name, bool genres) {
        auto *section = new QWidget(split);
        section->setObjectName(name + "Section");
        section->setAccessibleName(genres ? tr("Genres or titles") : caption);
        auto *column = new QVBoxLayout(section);
        column->setContentsMargins(0, 2, 0, 0);
        column->setSpacing(3);
        column->setSizeConstraint(QLayout::SetNoConstraint);
        auto *heading = label(section, true);
        heading->setText(caption);
        auto *headingRow = new QHBoxLayout;
        headingRow->addWidget(heading, 1);
        if (genres) {
            genreHeading_ = heading;
            genreHeading_->setObjectName("genreHeading");
            genreOrder_ = new QToolButton(section);
            genreOrder_->setObjectName("genreOrder");
            genreOrder_->setCheckable(true);
            genreOrder_->setFocusPolicy(Qt::StrongFocus);
            headingRow->addWidget(genreOrder_);
        }
        column->addLayout(headingRow);
        auto *tree = new SidebarList(section);
        tree->setObjectName(name);
        tree->setAccessibleName(caption);
        tree->setHeaderLabels(genres ? QStringList{"Genre", "Last"} : QStringList{"Title"});
        tree->setHeaderHidden(true);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        if (genres) tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        auto *search = new PanelSearch(tree, section, genres ? Qt::DisplayRole : Qt::ToolTipRole);
        search->returnToEditor = [this] { editor_->setFocus(); };
        search->setObjectName(genres ? "genresSearch" : "historySearch");
        heading->setBuddy(search);
        if (genres) {
            genresSearch_ = search;
            search->activateRow = [this](bool) { return openGenreOrTitle(); };
            setTabOrder(genreOrder_, search);
        } else {
            historySearch_ = search;
            search->activateRow = [this](bool) { return openVisit(); };
            search->setPlaceholderText(tr("Search visit history"));
            search->setAccessibleName(search->placeholderText());
            search->setToolTip(tr("Search visit history (Ctrl+Shift+3). Up and Down move between matches. Enter returns to the passage."));
        }
        setTabOrder(search, tree);
        connect(tree, &QTreeWidget::itemClicked, this, [search] { search->activateCurrent(); });
        column->addWidget(search);
        column->addWidget(tree, 1);
        split->addWidget(section);
        return tree;
    };
    visits_ = addList(tr("Visit history"), "visitHistory", false);
    genres_ = addList(tr("Genres"), "genreList", true);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 1);
    connect(genreOrder_, &QToolButton::toggled, this, [this](bool byNumber) {
        settings_->setValue(orderPreference(editor_->model().isGeneric()), byNumber);
        settings_->sync();
        updateGenres();
    });
    layout->addWidget(split, 1);

    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateCurrent(); });
    editor_->model().observe(this, [this](const CrumbChange &change) { updateModel(change); });
    const QPointer<NavigationPanel> panel(this);
    editor_->onVirtualChanged = [panel] { if (panel) panel->updateCurrent(); };
    editor_->onViAccepted = [panel] { if (panel) panel->updateCurrent(); };
    reset();
}

void NavigationPanel::reset()
{
    historySearch_->clear();
    genresSearch_->clear();
    visitedLines_.clear();
    visits_->clear();
    updateMode();
    updateToday();
    updateGenres();
    updateCurrent();
}

void NavigationPanel::updateMode()
{
    const bool generic = editor_->model().isGeneric();
    for (auto *widget : {number_, title_, words_}) widget->setVisible(!generic);
    today_->setWordWrap(generic);
    today_->setAccessibleDescription(generic ? tr("Net change in manuscript words today, including deletions.") :
                                              tr("New crumbs written today."));
    const QString caption = generic ? tr("Titles") : tr("Genres");
    genreHeading_->setText(caption);
    genres_->setAccessibleName(caption);
    genres_->headerItem()->setText(0, generic ? tr("Title") : tr("Genre"));
    LayoutColumns::setOptionalColumn(genres_, 1, !generic);
    visits_->headerItem()->setText(0, generic ? tr("Paragraph") : tr("Title"));
    genresSearch_->setPlaceholderText(generic ? tr("Search titles") : tr("Search genres"));
    genresSearch_->setAccessibleName(genresSearch_->placeholderText());
    genresSearch_->setToolTip(generic ?
        tr("Search titles (Ctrl+Shift+4). Up and Down move between matches. Enter goes to the title.") :
        tr("Search genres (Ctrl+Shift+4). Up and Down move between matches. Enter adds the next crumb in the genre."));
    genreOrder_->setProperty("layoutDefaultOrder", generic);
    const QSignalBlocker blocked(genreOrder_);
    genreOrder_->setChecked(settings_->value(orderPreference(generic), generic).toBool());
}

void NavigationPanel::updateToday()
{
    const auto &model = editor_->model();
    const QString count = model.isGeneric() ? tr("New words written today: %1").arg(ManuscriptNumbers::format(model.todayWords()))
                                            : tr("New today   %1").arg(model.todayCount());
    if (today_->text() != count) {
        today_->setText(count);
        today_->setToolTip(count);
    }
}

bool NavigationPanel::isVisitLine(int row) const
{
    const auto &model = editor_->model();
    return row >= 0 && row < editor_->document()->blockCount() &&
           (model.isGeneric() ? model.paragraphRole(row) != ParagraphRole::Blank : model.record(row).valid);
}

void NavigationPanel::updateVisit(QTreeWidgetItem *item)
{
    const auto &model = editor_->model();
    const auto &record = model.record(static_cast<VisitItem *>(item)->caret.blockNumber());
    const QString full = model.isGeneric() ? record.text :
                         record.title.isEmpty() ? tr("Untitled") : record.title;
    if (item->toolTip(0) == full) return;
    item->setText(0, model.isGeneric() ? paragraphPreview(full) : full);
    item->setToolTip(0, full);
}

void NavigationPanel::updateCurrent()
{
    const QTextCursor caret = insertionPoint(editor_->textCursor());
    const int row = caret.blockNumber();
    const int virtualRows = editor_->virtualRows();
    const auto &model = editor_->model();
    if (!model.isGeneric()) {
        const auto &record = model.record(row);
        const bool valid = record.valid && !virtualRows;
        number_->setText(valid ? record.label + " " + QString::number(record.number)
                              : QString("%1 %2").arg(virtualRows ? "Blank line" : "Line").arg(row + 1 + virtualRows));
        title_->setText(valid ? record.title : QString());
        title_->setToolTip(title_->text());
        words_->setText(QString("Words   %1").arg(virtualRows ? 0 : record.words));
        QPalette colors = palette();
        if (valid && record.headerWarning)
            colors.setColor(QPalette::WindowText, EditorColors::warning(colors.window().color()));
        if (number_->palette() != colors) number_->setPalette(colors);
    }
    if (virtualRows || editor_->viSelectionActive() || !isVisitLine(row)) return;

    auto *item = static_cast<VisitItem *>(visitedLines_.value(row));
    if (item && item->caret.block() != caret.block()) {
        delete visits_->takeTopLevelItem(visits_->indexOfTopLevelItem(item));
        item = nullptr;
    }
    if (!item) {
        item = new VisitItem(caret);
        visitedLines_.insert(row, item);
        visits_->insertTopLevelItem(0, item);
    } else {
        item->caret = caret;
        if (visits_->topLevelItem(0) != item) {
            visits_->takeTopLevelItem(visits_->indexOfTopLevelItem(item));
            visits_->insertTopLevelItem(0, item);
        }
    }
    updateVisit(item);
}

void NavigationPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange || !genres_ || editor_->model().isGeneric()) return;
    updateCurrent();
    // Existing rows carry explicit warning ink. Recolor in place to preserve
    // the current row, its scroll position, and the keyboard search.
    for (int row = 0; row < genres_->topLevelItemCount(); ++row) {
        auto *item = genres_->topLevelItem(row);
        if (item->data(1, Qt::ForegroundRole).isValid())
            markNumber(item, 1, true, palette());
    }
}

void NavigationPanel::updateModel(const CrumbChange &change)
{
    if (change.modeChanged) {
        genresSearch_->clear();
        updateMode();
    }
    updateToday();
    if (change.structureChanged || change.modeChanged) {
        visitedLines_.clear();
        for (int i = 0; i < visits_->topLevelItemCount();) {
            auto *visit = static_cast<VisitItem *>(visits_->topLevelItem(i));
            const int row = visit->caret.blockNumber();
            if (!isVisitLine(row) || visitedLines_.contains(row)) {
                delete visits_->takeTopLevelItem(i);
            } else {
                visitedLines_.insert(row, visit);
                updateVisit(visit);
                ++i;
            }
        }
    } else {
        for (int i = change.firstLine; i <= change.lastLine; ++i) {
            auto *visit = visitedLines_.value(i);
            if (!visit) continue;
            if (isVisitLine(i)) updateVisit(visit);
            else {
                visitedLines_.remove(i);
                delete visits_->takeTopLevelItem(visits_->indexOfTopLevelItem(visit));
            }
        }
    }
    if (change.modeChanged || (editor_->model().isGeneric() ? change.titlesChanged : change.genresChanged))
        updateGenres();
    updateCurrent();
    if (!historySearch_->text().isEmpty()) historySearch_->refresh();
}

void NavigationPanel::updateGenres()
{
    const bool byNumber = genreOrder_->isChecked();
    const auto &model = editor_->model();
    const bool generic = model.isGeneric();
    genreOrder_->setText(byNumber ? "# ↑" : "A–Z");
    QString order;
    if (generic) order = byNumber ? tr("Titles in document order") : tr("Titles ordered alphabetically");
    else order = byNumber ? tr("Genres ordered by number, then name") : tr("Genres ordered alphabetically");
    genreOrder_->setAccessibleName(order);
    genreOrder_->setToolTip(order + tr(". Switch order (Shift+F7)"));
    const QVariant selected = genres_->currentItem() ? genres_->currentItem()->data(0, Qt::UserRole) : QVariant();
    genres_->clear();
    QList<QTreeWidgetItem *> entries;
    QTreeWidgetItem *retain = nullptr;
    if (generic) {
        auto lines = model.titleLines();
        if (!byNumber) {
            QCollator collator;
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            std::stable_sort(lines.begin(), lines.end(), [&](int a, int b) {
                return collator.compare(QStringView(model.record(a).text).trimmed(),
                                        QStringView(model.record(b).text).trimmed()) < 0;
            });
        }
        const auto previous = selected.value<QTextBlock>();
        for (int line : lines) {
            const QString title = model.record(line).text.trimmed();
            const auto block = editor_->document()->findBlockByNumber(line);
            auto *item = new QTreeWidgetItem(QStringList{title});
            item->setToolTip(0, title);
            item->setData(0, Qt::UserRole, QVariant::fromValue(block));
            entries.append(item);
            if (block == previous) retain = item;
        }
    } else {
        auto rows = model.genres(); // Alphabetical order also breaks number ties.
        if (byNumber) {
            std::stable_sort(rows.begin(), rows.end(), [](const CrumbGenre &a, const CrumbGenre &b) {
                return a.number < b.number;
            });
        }
        for (const auto &genre : rows) {
            auto *item = new QTreeWidgetItem(QStringList{genre.name, "#" + QString::number(genre.number)});
            item->setData(0, Qt::UserRole, genre.key);
            item->setToolTip(0, genre.name);
            markNumber(item, 1, genre.warning, palette());
            if (!genre.canAppend) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            entries.append(item);
            if (genre.key == selected.toString()) retain = item;
        }
    }
    genres_->addTopLevelItems(entries);
    if (retain) genres_->setCurrentItem(retain);
    genresSearch_->refresh();
}

void NavigationPanel::toggleGenreOrder()
{
    genreOrder_->toggle();
}

bool NavigationPanel::openVisit()
{
    const auto *visit = static_cast<VisitItem *>(visits_->currentItem());
    if (!visit || visit->caret.isNull() || !jumpTo) return false;
    jumpTo(visit->caret.position());
    return true;
}

bool NavigationPanel::openGenreOrTitle()
{
    const auto *item = genres_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return false;
    if (editor_->model().isGeneric()) {
        const auto block = item->data(0, Qt::UserRole).value<QTextBlock>();
        if (!block.isValid() || !jumpTo) return false;
        jumpTo(block.position());
    } else {
        editor_->appendCrumb(item->text(0));
    }
    editor_->setFocus();
    return true;
}

void NavigationPanel::focusHistory()
{
    visits_->parentWidget()->show();
    historySearch_->focusSearch();
}

void NavigationPanel::focusGenres()
{
    genres_->parentWidget()->show();
    genresSearch_->focusSearch();
}
