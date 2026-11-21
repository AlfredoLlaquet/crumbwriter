#include "synonymresults.h"
#include "manuscripttext.h"

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QTableView>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>

namespace {
struct Occurrence {
    int start, end, line = 0;
};

struct Occurrences {
    QString text;
    QList<int> starts;
    QList<Occurrence> hits;
};

QString literalPattern(const QString &alias)
{
    // Both forms of the apostrophe occupy one UTF-16 unit. Report the exact
    // original spelling and offsets even when curly and straight marks differ.
    QString normalized = alias;
    normalized.replace(QChar(0x2019), u'\'');
    const auto fields = normalized.split(u'\'');
    QStringList escaped;
    for (const QString &field : fields) escaped.append(QRegularExpression::escape(field));
    const QString literal = escaped.join(QStringLiteral("['’]"));
    return QStringLiteral("(?<![\\p{L}\\p{M}\\p{N}_])(?=(%1)(?![\\p{L}\\p{M}\\p{N}_]))").arg(literal);
}

Occurrences findOccurrences(const QString &text, const QStringList &aliases, const std::atomic_bool &cancel)
{
    Occurrences results;
    results.text = text;
    results.starts.append(0);
    for (qsizetype i = text.indexOf(u'\n'); i >= 0; i = text.indexOf(u'\n', i + 1)) {
        if (cancel.load(std::memory_order_relaxed)) return {};
        results.starts.append(int(i + 1));
    }
    for (const QString &alias : aliases) {
        if (cancel.load(std::memory_order_relaxed)) return {};
        const QRegularExpression pattern(literalPattern(alias), QRegularExpression::CaseInsensitiveOption |
                                                                 QRegularExpression::UseUnicodePropertiesOption);
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            if (cancel.load(std::memory_order_relaxed)) return {};
            const auto match = it.next();
            results.hits.append({int(match.capturedStart(1)), int(match.capturedEnd(1))});
        }
    }
    // Two different aliases can overlap. Keep both matches, deduplicating
    // only when the aliases designate the exact same manuscript span.
    auto &hits = results.hits;
    std::sort(hits.begin(), hits.end(), [](const Occurrence &a, const Occurrence &b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });
    hits.erase(std::unique(hits.begin(), hits.end(), [](const Occurrence &a, const Occurrence &b) {
        return a.start == b.start && a.end == b.end;
    }), hits.end());
    int line = 0;
    for (auto &hit : hits) {
        while (line + 1 < results.starts.size() && results.starts[line + 1] <= hit.start) ++line;
        hit.line = line;
    }
    return results;
}
}

class SynonymMatchModel final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    Occurrences results;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(results.hits.size());
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
        if (!index.isValid() || index.row() >= rowCount()) return {};
        const auto &hit = results.hits[index.row()];
        if (role == Qt::UserRole) return hit.start;
        if (role == Qt::UserRole + 1) return hit.end;
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole) return {};
        if (index.column() == 0) return hit.line + 1;
        if (index.column() == 1) return results.text.mid(hit.start, hit.end - hit.start);
        const int first = results.starts[hit.line];
        const int last = hit.line + 1 < results.starts.size() ? results.starts[hit.line + 1] - 1
                                                               : int(results.text.size());
        const int left = std::max(first, hit.start - 50);
        const int right = std::min(last - hit.end, 70) + hit.end;
        QString excerpt = results.text.mid(left, right - left);
        if (left > first) excerpt.prepend(QChar(0x2026));
        if (right < last) excerpt.append(QChar(0x2026));
        return excerpt;
    }
    void setResults(Occurrences next)
    {
        beginResetModel();
        results = std::move(next);
        endResetModel();
    }
};

SynonymResults::SynonymResults(QTextDocument *document, QWidget *parent)
    : QDialog(parent), document_(document), model_(new SynonymMatchModel(this)),
      view_(new QTableView(this)), status_(new QLabel(this))
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

    worker_.setMaxThreadCount(1);
    timer_.setSingleShot(true);
    timer_.setInterval(220);
    connect(&timer_, &QTimer::timeout, this, &SynonymResults::search);
    connect(document, &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (removed || added) invalidate();
    });
    resize(QSize(760, 420).boundedTo(parent->screen()->availableGeometry().size() * 0.85));
}

SynonymResults::~SynonymResults()
{
    if (cancel_) cancel_->store(true);
    worker_.clear();
    worker_.waitForDone();
}

void SynonymResults::invalidate()
{
    current_ = false;
    if (cancel_) cancel_->store(true);
    if (model_->rowCount()) model_->setResults({});
    go_->setEnabled(false);
    if (isVisible() && !aliases_.isEmpty()) {
        status_->setText(QStringLiteral("Finding occurrences…"));
        timer_.start();
    }
}

void SynonymResults::showGroup(const QStringList &aliases)
{
    if (aliases.isEmpty()) return;
    if (aliases != aliases_) {
        aliases_ = aliases;
        invalidate();
    }
    setWindowTitle(QStringLiteral("Occurrences — %1").arg(aliases.first()));
    show();
    raise();
    activateWindow();
    if (!current_) search();
    view_->setFocus();
}

void SynonymResults::reset()
{
    aliases_.clear();
    hide();
    invalidate();
}

void SynonymResults::hideEvent(QHideEvent *event)
{
    timer_.stop();
    if (cancel_) cancel_->store(true);
    QDialog::hideEvent(event);
}

void SynonymResults::search()
{
    timer_.stop();
    if (!isVisible() || !document_ || aliases_.isEmpty()) return;
    if (cancel_) cancel_->store(true);
    worker_.clear();
    const auto stop = cancel_ = std::make_shared<std::atomic_bool>(false);
    const QString text = ManuscriptText::documentText(document_);
    status_->setText(QStringLiteral("Finding occurrences…"));
    worker_.start([this, text, aliases = aliases_, stop] {
        auto results = findOccurrences(text, aliases, *stop);
        if (stop->load()) return;
        QMetaObject::invokeMethod(this, [this, stop, results = std::move(results)]() mutable {
            if (stop->load()) return;
            current_ = true;
            model_->setResults(std::move(results));
            const int n = model_->rowCount();
            status_->setText(n == 1 ? QStringLiteral("1 occurrence") : QStringLiteral("%1 occurrences").arg(n));
            if (n) view_->selectRow(0);
        }, Qt::QueuedConnection);
    });
}

void SynonymResults::activate(int row)
{
    if (!current_ || row < 0 || row >= model_->rowCount() || !jumpToMatch) return;
    const auto hit = model_->results.hits.at(row);
    hide();
    auto *owner = parentWidget()->window();
    owner->raise();
    owner->activateWindow();
    jumpToMatch(hit.start, hit.end);
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
