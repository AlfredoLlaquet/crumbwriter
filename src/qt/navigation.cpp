#include "navigation.h"
#include "crumbtitlelabel.h"
#include "guillemetmarkup.h"
#include "layoutcolumns.h"
#include "editorcolors.h"
#include <QEvent>
#include "manuscripteditor.h"
#include "manuscriptnumbers.h"
#include "panelsearch.h"
#include "todaycountdialog.h"

#include <QAction>
#include <QTextDocument>
#include <QDialog>
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
constexpr int CanAppendGenreRole = Qt::UserRole + 2;

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
    info->setMinimumSize(1, 1);
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    number_ = label(info, true);
    number_->setObjectName("currentCrumb");
    QFont numberFont = number_->font();
    numberFont.setPointSizeF(qMax(12.0, numberFont.pointSizeF() * 1.25));
    number_->setFont(numberFont);
    title_ = new CrumbTitleLabel(info);
    words_ = label(info);
    words_->setObjectName("wordCount");
    todayHeading_ = label(info);
    todayHeading_->setObjectName("todayHeading");
    todayHeading_->setText(tr("New words written today"));
    todayHeading_->setWordWrap(true);
    today_ = label(info);
    today_->setObjectName("todayCount");
    for (QLabel *widget : {number_, static_cast<QLabel *>(title_), words_, todayHeading_, today_}) infoLayout->addWidget(widget);
    infoLayout->setAlignment(Qt::AlignTop);
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
        auto *search = new PanelSearch(tree, section, genres ? TitleOutline::searchRole : Qt::ToolTipRole);
        search->returnToEditor = [this, genres] {
            editor_->setFocus();
            if (genres && editor_->model().isGeneric())
                outline_.follow(genres_, editor_->textCursor().blockNumber());
        };
        search->setObjectName(genres ? "genresSearch" : "historySearch");
        heading->setBuddy(search);
        if (genres) {
            genresSearch_ = search;
            search->activateRow = [this](bool alternate) { return openGenreOrTitle(alternate); };
            genreMentions_ = new QAction(tr("Find all genre occurrences\tCtrl+Enter"), tree);
            tree->addAction(genreMentions_);
            tree->setContextMenuPolicy(Qt::ActionsContextMenu);
            connect(genreMentions_, &QAction::triggered, search, [search] { search->activateCurrent(true); });
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
    previousBlockCount_ = editor_->document()->blockCount();
    connect(editor_->document(), &QTextDocument::contentsChange, this,
            [this](int position, int, int added) { trackInsertedLines(position, added); });
    const QPointer<NavigationPanel> panel(this);
    editor_->onVirtualChanged = [panel] { if (panel) panel->updateCurrent(); };
    editor_->onViAccepted = [panel] { if (panel) panel->updateCurrent(); };
    editor_->onLiteralGuillemetsChanged = [panel] {
        if (!panel) return;
        panel->updateGenres();
        for (auto *visit : panel->visitedLines_) panel->updateVisit(visit);
        if (!panel->historySearch_->text().isEmpty()) panel->historySearch_->refresh();
    };
    reset();
}

void NavigationPanel::reset()
{
    if (todayDialog_) todayDialog_->reject();
    title_->closeFullTitle();
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
    for (QLabel *widget : {number_, static_cast<QLabel *>(title_), words_}) widget->setVisible(!generic);
    todayHeading_->setVisible(generic);
    today_->setFont(generic ? number_->font() : words_->font());
    sizeInformation();
    today_->setAccessibleDescription(generic ? tr("Net change in manuscript words today, including deletions.") :
                                              tr("New crumbs written today."));
    const QString caption = generic ? tr("Titles") : tr("Genres");
    genreHeading_->setText(caption);
    genreMentions_->setVisible(!generic);
    genres_->setAccessibleName(caption);
    genres_->headerItem()->setText(0, generic ? tr("Title") : tr("Genre"));
    LayoutColumns::setOptionalColumn(genres_, 1, !generic);
    visits_->headerItem()->setText(0, generic ? tr("Paragraph") : tr("Title"));
    genresSearch_->setPlaceholderText(generic ? tr("Search titles") : tr("Search genres"));
    genresSearch_->setAccessibleName(genresSearch_->placeholderText());
    genresSearch_->setToolTip(generic ?
        tr("Search titles (Ctrl+Shift+4). Up and Down move between matches. Enter goes to the title.") :
        tr("Search genres (Ctrl+Shift+4). Up and Down move between matches. Enter adds the next crumb. Ctrl+Enter finds all genre occurrences."));
    genreOrder_->setProperty("layoutDefaultOrder", generic);
    const QSignalBlocker blocked(genreOrder_);
    genreOrder_->setChecked(settings_->value(orderPreference(generic), generic).toBool());
}

void NavigationPanel::sizeInformation()
{
    const bool generic = editor_->model().isGeneric();
    auto *info = today_->parentWidget();
    auto *layout = info->layout();
    const int line = fontMetrics().lineSpacing();
    const int gap = qMax(1, line / (generic ? 4 : 3));
    layout->setSpacing(gap);
    const int bottom = qMax(2, line / 2);
    layout->setContentsMargins(0, generic ? gap : 0, 0, bottom);
    const int height = generic
        ? todayHeading_->fontMetrics().height() + today_->fontMetrics().height() + 2 * gap
        : number_->fontMetrics().height() + title_->height() + words_->fontMetrics().height()
          + today_->fontMetrics().height() + 3 * gap;
    info->setMinimumHeight(height + bottom);
}

void NavigationPanel::updateToday()
{
    const auto &model = editor_->model();
    const QString count = model.isGeneric() ? ManuscriptNumbers::format(model.todayWords())
                                            : tr("New today   %1").arg(ManuscriptNumbers::format(model.todayCount()));
    if (today_->text() != count) {
        today_->setText(count);
        const QString spoken = model.isGeneric() ? tr("New words written today: %1").arg(count) : count;
        today_->setAccessibleName(spoken);
        today_->setToolTip(spoken);
    }
}

void NavigationPanel::showTodayCountDialog()
{
    editor_->model().refreshDay();
    if (!todayDialog_ || !todayDialog_->isVisible())
        todayDialog_ = new TodayCountDialog(editor_->model(), this);
    todayDialog_->show();
    todayDialog_->raise();
    todayDialog_->activateWindow();
}

bool NavigationPanel::isVisitLine(int row) const
{
    const auto &model = editor_->model();
    return row >= 0 && row < editor_->document()->blockCount() &&
           (model.isGeneric() ? !model.record(row).blank && model.paragraphRole(row) != ParagraphRole::TitleMarker
                              : model.record(row).valid);
}

void NavigationPanel::updateVisit(QTreeWidgetItem *item)
{
    const auto &model = editor_->model();
    const auto &record = model.record(static_cast<VisitItem *>(item)->caret.blockNumber());
    QString full = model.isGeneric() ? record.text : record.title.isEmpty() ? tr("Untitled") : record.title;
    if (model.isGeneric() && !editor_->literalGuillemets() && full.contains(u'«'))
        full = GuillemetMarkup::visibleText(full);
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
        title_->setTitle(valid ? record.title : QString());
        words_->setText(QString("Words   %1").arg(virtualRows ? 0 : record.words));
        QPalette colors = palette();
        if (valid && record.headerWarning)
            colors.setColor(QPalette::WindowText, EditorColors::warning(colors.window().color()));
        if (number_->palette() != colors) number_->setPalette(colors);
    }
    if (model.isGeneric() && !genresSearch_->hasFocus() && !genres_->hasFocus())
        outline_.follow(genres_, row);
    if (!virtualRows && !editor_->viSelectionActive() && isVisitLine(row)) rememberVisit(caret);
}

void NavigationPanel::rememberVisit(QTextCursor caret)
{
    const int row = caret.blockNumber();
    // Appending at a visited paragraph's end must not move its stored visit
    // across the inserted newline. A visit in the interior follows its text.
    caret.setKeepPositionOnInsert(caret.atBlockEnd());
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

void NavigationPanel::trackInsertedLines(int position, int added)
{
    const auto *document = editor_->document();
    const int count = document->blockCount();
    const int newLines = count - previousBlockCount_;
    previousBlockCount_ = count;
    if (newLines <= 0 || !added || editor_->isLoadingText()) return;
    const int end = int(qMin<qint64>(qint64(position) + added, document->characterCount() - 1));
    const auto last = document->findBlock(end);
    // A paste ending at a paragraph's beginning has left that paragraph intact.
    const int after = last.blockNumber() + (last.position() == end ? 0 : 1);
    for (int line = qMax(0, after - newLines); line < after; ++line)
        if (isVisitLine(line) && !visitedLines_.contains(line))
            rememberVisit(QTextCursor(document->findBlockByNumber(line)));
    updateCurrent();
}

void NavigationPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (genres_ && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange))
        sizeInformation();
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
        if (todayDialog_) todayDialog_->reject();
        title_->closeFullTitle();
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
        const auto refreshVisit = [this](int i) {
            auto *visit = visitedLines_.value(i);
            if (!visit) return;
            if (isVisitLine(i)) updateVisit(visit);
            else {
                visitedLines_.remove(i);
                delete visits_->takeTopLevelItem(visits_->indexOfTopLevelItem(visit));
            }
        };
        for (int i = change.firstLine; i <= change.lastLine; ++i) refreshVisit(i);
        if (editor_->model().isGeneric())
            for (int i : change.otherLines) refreshVisit(i); // A neighboring row may have become a marker.
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
    const auto *current = genres_->currentItem();
    const QVariant selected = current ? current->data(0, Qt::UserRole) : QVariant();
    if (generic) {
        outline_.rebuild(genres_, model, editor_->document(), byNumber, editor_->literalGuillemets());
        if (genresSearch_->text().isEmpty()) outline_.follow(genres_, editor_->textCursor().blockNumber());
        else genresSearch_->refresh();
        return;
    }
    outline_.clear();
    genres_->clear();
    genres_->setRootIsDecorated(false);
    QList<QTreeWidgetItem *> entries;
    QTreeWidgetItem *retain = nullptr;
    auto rows = model.genres(); // Alphabetical order also breaks number ties.
    if (byNumber) {
        std::stable_sort(rows.begin(), rows.end(), [](const CrumbGenre &a, const CrumbGenre &b) {
            return a.number < b.number;
        });
    }
    for (const auto &genre : rows) {
        auto *item = new QTreeWidgetItem(QStringList{genre.name, "#" + QString::number(genre.number)});
        item->setData(0, Qt::UserRole, genre.key);
        item->setData(0, TitleOutline::searchRole, item->text(0));
        item->setData(1, TitleOutline::searchRole, item->text(1));
        item->setToolTip(0, genre.name);
        markNumber(item, 1, genre.warning, palette());
        item->setData(0, CanAppendGenreRole, genre.canAppend);
        entries.append(item);
        if (genre.key == selected.toString()) retain = item;
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

bool NavigationPanel::openGenreOrTitle(bool occurrences)
{
    const auto *item = genres_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return false;
    if (editor_->model().isGeneric()) {
        const auto target = item->data(0, Qt::UserRole);
        if (!jumpTo) return false;
        if (item->data(0, TitleOutline::endRole).toBool()) jumpTo(editor_->model().documentEndPosition());
        else {
            const auto block = target.value<QTextBlock>();
            if (!block.isValid()) return false;
            jumpTo(block.position());
            if (editor_->model().paragraphRole(block.blockNumber()) == ParagraphRole::DocumentTitle)
                editor_->topCurrentCrumb();
        }
    } else if (occurrences) {
        return genreOccurrences && genreOccurrences(item->text(0));
    } else {
        if (!item->data(0, CanAppendGenreRole).toBool()) return false;
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
