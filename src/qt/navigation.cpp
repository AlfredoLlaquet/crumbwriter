#include "navigation.h"
#include "manuscripteditor.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QSplitter>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
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
        connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
            if (open) open(item);
        });
    }
    std::function<void(QTreeWidgetItem *)> open;
protected:
    void focusInEvent(QFocusEvent *e) override
    {
        typed_.clear();
        interval_.invalidate();
        QTreeWidget::focusInEvent(e);
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentItem()) {
            if (open) open(currentItem());
            e->accept();
        } else {
            QTreeWidget::keyPressEvent(e);
        }
    }
    void keyboardSearch(const QString &text) override
    {
        if (!interval_.isValid() || interval_.elapsed() > QApplication::keyboardInputInterval())
            typed_.clear();
        interval_.start();
        typed_ += text;
        for (int i = 0; i < topLevelItemCount(); ++i) {
            auto *item = topLevelItem(i);
            if (item->text(0).contains(typed_, Qt::CaseInsensitive) ||
                item->text(1).contains(typed_, Qt::CaseInsensitive)) {
                setCurrentItem(item);
                scrollToItem(item);
                break;
            }
        }
    }
private:
    QString typed_;
    QElapsedTimer interval_;
};

class VisitItem final : public QTreeWidgetItem {
public:
    explicit VisitItem(const QTextCursor &at) : block(at.block()), caret(at) {}
    QTextBlock block;
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
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    item->setForeground(column, warning ? QBrush(QColor(dark ? "#ff8488" : "#bc102a")) : QBrush());
}

QTextCursor insertionPoint(QTextCursor c)
{
    c.clearSelection();
    return c;
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

NavigationPanel::NavigationPanel(ManuscriptEditor *editor, QWidget *parent)
    : QWidget(parent), editor_(editor)
{
    setObjectName("sidebar");
    setMinimumSize(1, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    number_ = label(this, true);
    number_->setObjectName("currentCrumb");
    QFont numberFont = number_->font();
    numberFont.setPointSizeF(qMax(12.0, numberFont.pointSizeF() * 1.25));
    number_->setFont(numberFont);
    title_ = label(this);
    title_->setObjectName("crumbTitle");
    title_->setWordWrap(true);
    title_->setMaximumHeight(title_->fontMetrics().lineSpacing() * 4);
    words_ = label(this);
    words_->setObjectName("wordCount");
    session_ = label(this);
    session_->setObjectName("sessionCount");
    for (auto *widget : {number_, title_, words_, session_}) layout->addWidget(widget);

    auto *split = new QSplitter(Qt::Vertical, this);
    auto addList = [split](const QString &caption, const QString &name, bool numberFirst) {
        auto *section = new QWidget(split);
        auto *column = new QVBoxLayout(section);
        column->setContentsMargins(0, 2, 0, 0);
        column->setSpacing(3);
        column->setSizeConstraint(QLayout::SetNoConstraint);
        auto *heading = label(section, true);
        heading->setText(caption);
        column->addWidget(heading);
        auto *tree = new SidebarList(section);
        tree->setObjectName(name);
        tree->setAccessibleName(caption);
        tree->setColumnCount(2);
        tree->setHeaderLabels(numberFirst ? QStringList{"Number", "Title"} : QStringList{"Genre", "Last"});
        tree->setHeaderHidden(true);
        tree->header()->setStretchLastSection(numberFirst);
        tree->header()->setSectionResizeMode(numberFirst ? 0 : 1, QHeaderView::ResizeToContents);
        tree->header()->setSectionResizeMode(numberFirst ? 1 : 0, QHeaderView::Stretch);
        column->addWidget(tree, 1);
        split->addWidget(section);
        return tree;
    };
    auto *visits = addList("Visit history", "visitHistory", true);
    auto *genres = addList("Genres", "genreList", false);
    visits_ = visits;
    genres_ = genres;
    visits->open = [this](auto *item) { openVisit(item); };
    genres->open = [this](auto *item) { continueGenre(item); };
    layout->addWidget(split, 1);

    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateCurrent(); });
    editor_->model().observe(this, [this](const CrumbChange &change) { updateModel(change); });
    editor_->onVirtualChanged = [this] { updateCurrent(); };
    reset();
}

void NavigationPanel::reset()
{
    visitedLines_.clear();
    visits_->clear();
    updateGenres();
    updateCurrent();
}

void NavigationPanel::updateVisit(QTreeWidgetItem *item)
{
    const int row = static_cast<VisitItem *>(item)->block.blockNumber();
    const auto &record = editor_->model().record(row);
    const QString number = QString::number(record.number);
    if (item->text(0) != number) item->setText(0, number);
    if (item->text(1) != record.title) {
        item->setText(1, record.title);
        item->setToolTip(1, record.title);
    }
    if (item->data(0, Qt::UserRole) != QVariant(record.headerWarning)) {
        item->setData(0, Qt::UserRole, record.headerWarning);
        markNumber(item, 0, record.headerWarning, palette());
    }
}

void NavigationPanel::updateCurrent()
{
    const QTextCursor caret = insertionPoint(editor_->textCursor());
    const int row = caret.blockNumber();
    const auto &record = editor_->model().record(row);
    const int virtualRows = editor_->virtualRows();
    const bool valid = record.valid && !virtualRows;
    number_->setText(valid ? record.label + " " + QString::number(record.number)
                          : QString("%1 %2").arg(virtualRows ? "Blank line" : "Line").arg(row + 1 + virtualRows));
    title_->setText(valid ? record.title : QString());
    title_->setToolTip(title_->text());
    words_->setText(QString("Words   %1").arg(virtualRows ? 0 : record.words));
    session_->setText(QString("New this session   %1").arg(editor_->model().sessionCount()));
    QPalette colors = palette();
    if (valid && record.headerWarning)
        colors.setColor(QPalette::WindowText, colors.color(QPalette::Window).lightness() < 128
                                              ? QColor("#ff8488") : QColor("#bc102a"));
    if (number_->palette() != colors) number_->setPalette(colors);
    if (!valid) return;

    auto *item = static_cast<VisitItem *>(visitedLines_.value(row));
    if (item && item->block != caret.block()) {
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

void NavigationPanel::updateModel(const CrumbChange &change)
{
    if (change.structureChanged) {
        visitedLines_.clear();
        for (int i = visits_->topLevelItemCount() - 1; i >= 0; --i) {
            auto *visit = static_cast<VisitItem *>(visits_->topLevelItem(i));
            int row = visit->block.isValid() ? visit->block.blockNumber() : -1;
            if (!visit->block.isValid() || !editor_->model().record(row).valid ||
                visitedLines_.contains(row)) {
                delete visits_->takeTopLevelItem(i);
            } else {
                visitedLines_.insert(row, visit);
                updateVisit(visit);
            }
        }
    } else {
        for (int i = change.firstLine; i <= change.lastLine; ++i) {
            auto *visit = visitedLines_.value(i);
            if (!visit) continue;
            if (editor_->model().record(i).valid) updateVisit(visit);
            else {
                visitedLines_.remove(i);
                delete visits_->takeTopLevelItem(visits_->indexOfTopLevelItem(visit));
            }
        }
    }
    if (change.genresChanged) updateGenres();
    updateCurrent();
}

void NavigationPanel::updateGenres()
{
    QString selected;
    if (genres_->currentItem()) selected = genres_->currentItem()->data(0, Qt::UserRole).toString();
    genres_->clear();
    for (const auto &genre : editor_->model().genres()) {
        auto *item = new QTreeWidgetItem(genres_, {genre.name, "#" + QString::number(genre.number)});
        item->setData(0, Qt::UserRole, genre.key);
        item->setToolTip(0, genre.name);
        markNumber(item, 1, genre.warning, palette());
        if (!genre.canAppend) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        if (genre.key == selected) genres_->setCurrentItem(item);
    }
}

void NavigationPanel::openVisit(QTreeWidgetItem *item)
{
    const auto *visit = static_cast<VisitItem *>(item);
    if (visit->block.isValid() && jumpTo) jumpTo(visit->caret.position());
}

void NavigationPanel::continueGenre(QTreeWidgetItem *item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
    editor_->appendCrumb(item->text(0));
    editor_->setFocus();
}

void NavigationPanel::focusHistory()
{
    visits_->setFocus();
    if (!visits_->currentItem()) visits_->setCurrentItem(visits_->topLevelItem(0));
}

void NavigationPanel::focusGenres()
{
    genres_->setFocus();
    if (!genres_->currentItem()) genres_->setCurrentItem(genres_->topLevelItem(0));
}
