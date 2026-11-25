#include "backupcomparison.h"
#include "documentstore.h"
#include "manuscripteditor.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>

namespace {
using Match = std::pair<int, int>;
using Matches = QList<Match>;

// Unique paragraphs form stable anchors in prose. Resolve short repetitive
// passages exactly and bound the work on large, completely rewritten passages.
Matches anchors(const QStringList &a, int ai, int ae, const QStringList &b, int bi, int be)
{
    QHash<QString, int> left, right;
    auto index = [](auto &map, const QStringList &lines, int from, int to) {
        for (int i = from; i < to; ++i) {
            auto it = map.find(lines[i]);
            if (it == map.end()) map.insert(lines[i], i);
            else it.value() = -1;
        }
    };
    index(left, a, ai, ae);
    index(right, b, bi, be);
    Matches candidates;
    QList<int> tails, predecessor;
    for (int i = ai; i < ae; ++i) {
        const int j = right.value(a[i], -1);
        if (j < 0 || left.value(a[i]) != i) continue;
        const int n = int(candidates.size());
        candidates.append({i, j});
        auto t = std::lower_bound(tails.begin(), tails.end(), j,
                [&](int k, int value) { return candidates[k].second < value; });
        predecessor.append(t == tails.begin() ? -1 : *(t - 1));
        if (t == tails.end()) tails.append(n);
        else *t = n;
    }
    Matches result;
    for (int k = tails.isEmpty() ? -1 : tails.last(); k >= 0; k = predecessor[k])
        result.append(candidates[k]);
    std::reverse(result.begin(), result.end());
    return result;
}

void matchLines(const QStringList &a, int ai, int ae, const QStringList &b, int bi, int be,
                int depth, Matches &out)
{
    while (ai < ae && bi < be && a[ai] == b[bi]) out.append({ai++, bi++});
    const int originalEnd = ae;
    while (ai < ae && bi < be && a[ae - 1] == b[be - 1]) { --ae; --be; }
    const int n = ae - ai, m = be - bi;
    if (n && m && qint64(n) * m <= 120000) {
        const int stride = m + 1;
        QList<int> length((n + 1) * stride, 0);
        for (int i = n - 1; i >= 0; --i)
            for (int j = m - 1; j >= 0; --j) {
                const int p = i * stride + j;
                length[p] = a[ai + i] == b[bi + j] ? 1 + length[p + stride + 1]
                                                   : std::max(length[p + 1], length[p + stride]);
            }
        for (int i = 0, j = 0; i < n && j < m;) {
            if (a[ai+i] == b[bi+j]) { out.append({ai + i++, bi + j++}); continue; }
            const int p = i * stride + j;
            if (length[p + stride] >= length[p + 1]) ++i; else ++j;
        }
    } else if (n && m && depth < 24) {
        const Matches stable = anchors(a, ai, ae, b, bi, be);
        if (!stable.isEmpty()) {
            for (const auto &pair : stable) {
                matchLines(a, ai, pair.first, b, bi, pair.second, depth + 1, out);
                out.append(pair);
                ai = pair.first + 1;
                bi = pair.second + 1;
            }
            matchLines(a, ai, ae, b, bi, be, depth + 1, out);
        }
    }
    for (int k = 0; k < originalEnd - ae; ++k) out.append({ae + k, be + k});
}

QStringList paragraphs(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    if (text.isEmpty()) return {};
    if (text.endsWith(u'\n')) text.chop(1);
    return text.split(u'\n');
}

QString location(int start, int count)
{
    return count == 1 ? QString::number(start + 1)
                      : QStringLiteral("%1–%2").arg(start + 1).arg(start + count);
}
}

QList<BackupDiff::Change> BackupDiff::compare(const QStringList &a, const QStringList &b)
{
    Matches matches;
    matchLines(a, 0, int(a.size()), b, 0, int(b.size()), 0, matches);
    matches.append({int(a.size()), int(b.size())});
    QList<Change> result;
    int i = 0, j = 0;
    for (const auto &pair : matches) {
        if (pair.first != i || pair.second != j)
            result.append({i, pair.first - i, j, pair.second - j});
        i = pair.first + 1;
        j = pair.second + 1;
    }
    return result;
}

BackupComparison::BackupComparison(ManuscriptEditor *editor, DocumentStore *store)
    : QDialog(editor), editor_(editor), store_(store)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("backupComparisonDialog");
    setWindowTitle(tr("Compare with Startup Backup"));
    auto *layout = new QVBoxLayout(this);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::PlainText);
    layout->addWidget(summary_);
    changes_ = new QListWidget(this);
    changes_->setAccessibleName(tr("Changed passages"));
    changes_->setObjectName("backupChanges");
    layout->addWidget(changes_, 1);
    auto *columns = new QSplitter(this);
    auto column = [&](const QString &name) {
        auto *widget = new QWidget(columns);
        auto *box = new QVBoxLayout(widget);
        box->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(name, widget);
        auto *view = new QPlainTextEdit(widget);
        view->setFont(editor->font());
        view->setReadOnly(true);
        view->setTabChangesFocus(true);
        label->setBuddy(view);
        box->addWidget(label);
        box->addWidget(view);
        columns->addWidget(widget);
        return view;
    };
    beforeView_ = column(tr("Startup &backup"));
    afterView_ = column(tr("Current &manuscript"));
    layout->addWidget(columns, 2);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    previous_ = buttons->addButton(tr("&Previous"), QDialogButtonBox::ActionRole);
    next_ = buttons->addButton(tr("&Next"), QDialogButtonBox::ActionRole);
    auto *reload = buttons->addButton(tr("&Refresh"), QDialogButtonBox::ActionRole);
    previous_->setToolTip(tr("Previous change (Shift+F3)"));
    next_->setToolTip(tr("Next change (F3)"));
    connect(previous_, &QPushButton::clicked, this, [this] { changes_->setCurrentRow(changes_->currentRow() - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { changes_->setCurrentRow(changes_->currentRow() + 1); });
    connect(reload, &QPushButton::clicked, this, &BackupComparison::refresh);
    auto key = [this](const QKeySequence &sequence, QPushButton *button) {
        auto *shortcut = new QShortcut(sequence, this);
        connect(shortcut, &QShortcut::activated, button, &QPushButton::click);
    };
    key(QKeySequence(QStringLiteral("Shift+F3")), previous_);
    key(QKeySequence(Qt::Key_F3), next_);
    key(QKeySequence(Qt::Key_F5), reload);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(changes_, &QListWidget::currentRowChanged, this, &BackupComparison::display);
    layout->addWidget(buttons);
    resize(QSize(820, 560).boundedTo(screen()->availableGeometry().size() * .9));
    changes_->setFocus();
}

void BackupComparison::refresh()
{
    int row = changes_->currentRow();
    changes_->clear();
    ranges_.clear();
    before_.clear();
    after_.clear();
    const QString path = store_->backupPath();
    QFile file(path);
    if (path.isEmpty()) {
        summary_->setText(tr("This manuscript has no startup backup. A backup is made when you open an existing file with startup backups enabled."));
    } else if (!file.open(QIODevice::ReadOnly)) {
        summary_->setText(tr("Cannot read %1. %2").arg(path, file.errorString()));
    } else {
        QByteArray content = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            summary_->setText(tr("Cannot read %1. %2").arg(path, file.errorString()));
            display(-1);
            return;
        }
        if (content.startsWith("\xef\xbb\xbf")) content.remove(0, 3);
        before_ = paragraphs(QString::fromUtf8(content));
        after_ = paragraphs(editor_->manuscriptText());
        ranges_ = BackupDiff::compare(before_, after_);
        summary_->setText(ranges_.isEmpty() ? tr("No paragraph changes since %1.").arg(QFileInfo(path).fileName())
                                             : tr("%1 changed passage(s) since %2.").arg(ranges_.size()).arg(QFileInfo(path).fileName()));
        for (const auto &r : ranges_) {
            if (!r.oldCount) changes_->addItem(tr("Added at manuscript line %1").arg(location(r.newStart, r.newCount)));
            else if (!r.newCount) changes_->addItem(tr("Removed from backup line %1").arg(location(r.oldStart, r.oldCount)));
            else changes_->addItem(tr("Backup %1  →  Manuscript %2").arg(location(r.oldStart, r.oldCount), location(r.newStart, r.newCount)));
        }
    }
    if (ranges_.isEmpty()) display(-1);
    else changes_->setCurrentRow(std::clamp(row, 0, int(ranges_.size()) - 1));
}

void BackupComparison::display(int row)
{
    const bool valid = row >= 0 && row < ranges_.size();
    previous_->setEnabled(valid && row > 0);
    next_->setEnabled(valid && row + 1 < ranges_.size());
    if (!valid) { beforeView_->clear(); afterView_->clear(); return; }
    const auto &r = ranges_[row];
    beforeView_->setPlainText(before_.mid(r.oldStart, r.oldCount).join(u'\n'));
    afterView_->setPlainText(after_.mid(r.newStart, r.newCount).join(u'\n'));
}
