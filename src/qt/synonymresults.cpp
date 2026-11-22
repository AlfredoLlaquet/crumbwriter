#include "synonymresults.h"
#include "manuscripttext.h"
#include "synonymmatch.h"

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <QScreen>
#include <QTableView>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
#include <vector>

namespace {
struct Occurrence {
    int line, start, end; // UTF-16 columns within the physical line.
};

void collect(const QTextBlock &block, const SynonymMatcher &matcher, int group, std::vector<Occurrence> &rows)
{
    for (const auto &hit : matcher.mentions(block)) {
        if (hit.groups.contains(group))
            rows.push_back({block.blockNumber(), hit.start - block.position(), hit.end - block.position()});
    }
}
}

class SynonymMatchModel final : public QAbstractTableModel {
public:
    SynonymMatchModel(QTextDocument *document, QObject *parent) : QAbstractTableModel(parent), document_(document) {}
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(rows_.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 3; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
        case 0: return QStringLiteral("Line");
        case 1: return QStringLiteral("Match");
        case 2: return QStringLiteral("Passage");
        }
        return {};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!document_ || !index.isValid() || index.row() >= rowCount()) return {};
        const auto &hit = rows_.at(index.row());
        const auto block = document_->findBlockByNumber(hit.line);
        if (!block.isValid()) return {};
        if (role == Qt::UserRole) return block.position() + hit.start;
        if (role == Qt::UserRole + 1) return block.position() + hit.end;
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole) return {};
        if (index.column() == 0) return hit.line + 1;
        const auto text = ManuscriptText::blockText(block);
        if (index.column() == 1) return text.mid(hit.start, hit.end - hit.start);
        const int left = std::max(0, hit.start - 50);
        const int right = hit.end + std::min(70, int(text.size()) - hit.end);
        QString excerpt = text.mid(left, right - left);
        if (left > 0) excerpt.prepend(QChar(0x2026));
        if (right < text.size()) excerpt.append(QChar(0x2026));
        return excerpt;
    }

    void clear()
    {
        if (rows_.empty()) return;
        beginResetModel();
        rows_.clear();
        endResetModel();
    }

    void replaceLines(int first, int oldPast, int delta, const std::vector<Occurrence> &replacement)
    {
        const auto atLine = [this](int line) {
            return int(std::lower_bound(rows_.begin(), rows_.end(), line,
                       [](const Occurrence &entry, int n) { return entry.line < n; }) - rows_.begin());
        };
        const int from = atLine(first), to = atLine(oldPast), added = int(replacement.size());
        // Preserve the selected result and scroll position for edits elsewhere.
        if (to - from == added) {
            std::copy(replacement.begin(), replacement.end(), rows_.begin() + from);
            if (added) Q_EMIT dataChanged(index(from, 0), index(from + added - 1, 2));
        } else {
            if (from < to) {
                beginRemoveRows({}, from, to - 1);
                rows_.erase(rows_.begin() + from, rows_.begin() + to);
                endRemoveRows();
            }
            if (added) {
                beginInsertRows({}, from, from + added - 1);
                rows_.insert(rows_.begin() + from, replacement.begin(), replacement.end());
                endInsertRows();
            }
        }
        if (delta && from + added < rowCount()) {
            for (int row = from + added; row < rowCount(); ++row) rows_[row].line += delta;
            Q_EMIT dataChanged(index(from + added, 0), index(rowCount() - 1, 2));
        }
    }
private:
    QPointer<QTextDocument> document_;
    std::vector<Occurrence> rows_;
};

SynonymResults::SynonymResults(QTextDocument *document, SynonymMatcher *matcher, QWidget *parent)
    : QDialog(parent), document_(document), matcher_(matcher), model_(new SynonymMatchModel(document, this)),
      view_(new QTableView(this)), status_(new QLabel(this)), lineCount_(document->blockCount())
{
    setObjectName(QStringLiteral("synonymOccurrences"));
    setModal(false);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(status_);
    view_->setObjectName(QStringLiteral("synonymMatches"));
    view_->setAccessibleName(QStringLiteral("Manuscript occurrences"));
    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setTabKeyNavigation(false);
    view_->setWordWrap(false);
    view_->verticalHeader()->hide();
    view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->setColumnWidth(0, 65);
    view_->setColumnWidth(1, 150);
    view_->installEventFilter(this);
    layout->addWidget(view_, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    go_ = buttons->addButton(QStringLiteral("&Go to"), QDialogButtonBox::ActionRole);
    go_->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(go_, &QPushButton::clicked, this, [this] { activate(view_->currentIndex().row()); });
    connect(view_, &QTableView::activated, this, [this](QModelIndex i) { activate(i.row()); });
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](QModelIndex i) { go_->setEnabled(current_ && i.isValid()); });

    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &SynonymResults::scan);
    connect(document, &QTextDocument::contentsChange, this, &SynonymResults::edited);
    resize(QSize(760, 420).boundedTo(parent->screen()->availableGeometry().size() * 0.85));
}

void SynonymResults::invalidate()
{
    timer_.stop();
    nextLine_ = 0;
    current_ = false;
    model_->clear();
    go_->setEnabled(false);
    if (group_ >= 0 && isVisible()) {
        status_->setText(QStringLiteral("Finding occurrences…"));
        timer_.start(0);
    }
}

void SynonymResults::showGroup(int group)
{
    if (!matcher_ || group < 0 || group >= matcher_->groups().size()) return;
    const bool refresh = !current_ || group_ != group;
    group_ = group;
    setWindowTitle(QStringLiteral("Occurrences — %1").arg(matcher_->groups().at(group).aliases.first()));
    show();
    raise();
    activateWindow();
    if (refresh) invalidate();
    view_->setFocus();
}

void SynonymResults::reset()
{
    group_ = -1;
    hide();
    invalidate();
}

void SynonymResults::hideEvent(QHideEvent *event)
{
    timer_.stop();
    QDialog::hideEvent(event);
}

void SynonymResults::scan()
{
    if (!isVisible() || !document_ || !matcher_ || group_ < 0) return;
    const int first = nextLine_;
    std::vector<Occurrence> added;
    QElapsedTimer slice;
    slice.start();
    auto block = document_->findBlockByNumber(first);
    while (block.isValid()) {
        collect(block, *matcher_, group_, added);
        ++nextLine_;
        block = block.next();
        if (slice.elapsed() >= 6) break;
    }
    // Initial indexing yields to keystrokes and paints between short slices.
    model_->replaceLines(first, first, 0, added);
    if (block.isValid()) {
        timer_.start(0);
    } else {
        current_ = true;
        showCount();
    }
}

void SynonymResults::edited(int position, int removed, int added)
{
    if (!removed && !added) return;
    const int oldCount = lineCount_;
    lineCount_ = document_->blockCount();
    if (group_ < 0) return;
    if (!current_ || !isVisible()) { invalidate(); return; }

    const int final = document_->characterCount() - 1;
    const auto firstBlock = document_->findBlock(std::clamp(position, 0, final));
    const int first = firstBlock.blockNumber();
    const int last = document_->findBlock(int(std::clamp<qint64>(qint64(position) + added, 0, final))).blockNumber();
    const int delta = lineCount_ - oldCount;
    const int oldPast = last + 1 - delta;
    if (first < 0 || last < first || oldPast < first || oldPast > oldCount || last - first > 64) {
        invalidate();
        return;
    }
    std::vector<Occurrence> matches;
    for (auto block = firstBlock; block.isValid() && block.blockNumber() <= last; block = block.next())
        collect(block, *matcher_, group_, matches);
    model_->replaceLines(first, oldPast, delta, matches);
    showCount();
}

void SynonymResults::showCount()
{
    const int n = model_->rowCount();
    status_->setText(n == 1 ? QStringLiteral("1 occurrence") : QStringLiteral("%1 occurrences").arg(n));
    if (n && !view_->currentIndex().isValid()) view_->selectRow(0);
    go_->setEnabled(view_->currentIndex().isValid());
}

void SynonymResults::activate(int row)
{
    if (!current_ || row < 0 || row >= model_->rowCount() || !jumpToMatch) return;
    const auto chosen = model_->index(row, 0);
    const int start = chosen.data(Qt::UserRole).toInt(), end = chosen.data(Qt::UserRole + 1).toInt();
    hide();
    auto *owner = parentWidget()->window();
    owner->raise();
    owner->activateWindow();
    jumpToMatch(start, end);
}

bool SynonymResults::eventFilter(QObject *object, QEvent *event)
{
    if (object == view_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
            activate(view_->currentIndex().row());
            return true;
        }
    }
    return QDialog::eventFilter(object, event);
}
