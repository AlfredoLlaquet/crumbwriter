#include "dailycount.h"
#include "crumbmodel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>
#include <QtCore/qnumeric.h>
#include <algorithm>
#include <limits>

namespace {
QString statePath(const QString &manuscript)
{
    if (manuscript.isEmpty()) return {};
    const QFileInfo info(manuscript);
    const QString canonical = info.canonicalFilePath();
    const QString key = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    const auto hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    return data.filePath(QStringLiteral("crumbwriter/daily/") + QString::fromLatin1(hash) + ".json");
}

QByteArray beforeNewDay(const QByteArray &checkpoint, QDate date)
{
    auto data = QJsonDocument::fromJson(checkpoint).object();
    data.insert("date", date.toString(Qt::ISODate));
    data.insert("new", QJsonArray{});
    data.insert("count", 0);
    if (data.contains("words")) data.insert("baselineWords", data.value("words"));
    return QJsonDocument(data).toJson(QJsonDocument::Compact);
}

qint64 savedWordBaseline(const QJsonObject &data, qint64 fallback)
{
    const auto value = data.value("baselineWords").toInteger(-1);
    return value >= 0 && value <= std::numeric_limits<int>::max() ? value : fallback;
}
}

qint64 DailyCount::Adjustment::apply(qint64 now) const
{
    // A manuscript's measured change fits Qt's document position range.
    // The writer can enter the full signed integer range.
    const qint64 delta = now - measured;
    qint64 result;
    if (!qAddOverflow(value, delta, &result)) return result;
    return delta < 0 ? std::numeric_limits<qint64>::min() : std::numeric_limits<qint64>::max();
}

qint64 DailyCount::count() const
{
    return qMax<qint64>(0, crumbSetting_.apply(currentCount_));
}

qint64 DailyCount::wordChange() const
{
    return wordSetting_.apply(totalWords_ - baselineWords_);
}

QString DailyCount::setCount(bool words, qint64 value)
{
    Adjustment &setting = words ? wordSetting_ : crumbSetting_;
    const Adjustment before = setting;
    setting = {words ? value : qMax<qint64>(0, value), words ? totalWords_ - baselineWords_ : currentCount_};
    // A recovery draft can be the only named copy of an untitled manuscript.
    // Its text and the installed manuscript may have different checkpoints.
    QString error = writeState(lastDraftPath_, lastDraftCheckpoint_);
    if (!error.isEmpty()) {
        setting = before;
        return error;
    }
    error = writeState();
    if (!error.isEmpty()) {
        setting = before;
        const QString restoreError = writeState(lastDraftPath_, lastDraftCheckpoint_);
        if (!restoreError.isEmpty()) error += u'\n' + restoreError;
    }
    return error;
}

void DailyCount::indexOrigins()
{
    occupied_.fill(false, baseline_.size());
    originsByNumber_.clear();
    for (int id = 0; id < baseline_.size(); ++id)
        originsByNumber_[baseline_[id]].append(id);
    for (auto it = aliases_.cbegin(); it != aliases_.cend(); ++it)
        for (quint64 number : it.value())
            originsByNumber_[number].append(it.key());
}

void DailyCount::takeBaseline(QDate date)
{
    date_ = date;
    crumbSetting_ = {};
    wordSetting_ = {};
    baselineWords_ = totalWords_;
    cached_.clear();
    cachedLayout_ = {};
    committed_.clear();
    lastDraftPath_.clear();
    currentCount_ = 0;
    baseline_.clear();
    aliases_.clear();
    preferred_.clear();
    for (auto &row : rows_) {
        row.origin = row.number ? int(baseline_.size()) : -1;
        if (row.number) baseline_.append(row.number);
    }
    indexOrigins();
    occupied_.fill(true);
}

qsizetype DailyCount::headerExtent() const
{
    qsizetype count = rows_.size();
    while (count && !rows_[count - 1].number) --count;
    return count;
}

// A new checkpoint stops at the last header. Extra blank writing space and
// the final newline can differ between the buffer and the published file.
QByteArray DailyCount::shape(qsizetype count) const
{
    QByteArray bytes(count * qsizetype(sizeof(quint64)), Qt::Uninitialized);
    char *out = bytes.data();
    for (qsizetype i = 0; i < count; ++i) {
        qToBigEndian(i < rows_.size() ? rows_[i].number : quint64(0), out);
        out += sizeof(quint64);
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray DailyCount::snapshot()
{
    if (cachedLayout_.isEmpty()) {
        QJsonArray lines;
        for (int i = 0; i < rows_.size(); ++i)
            if (rows_[i].number && rows_[i].origin < 0) lines.append(i + 1);
        cachedLayout_ = {{"date", date_.toString(Qt::ISODate)}, {"shapeFormat", 2},
                         {"shape", QString::fromLatin1(shape(headerExtent()))},
                         {"count", currentCount_}, {"new", lines}};
    }
    if (cached_.isEmpty()) {
        QJsonObject data = cachedLayout_;
        data.insert("words", totalWords_);
        data.insert("baselineWords", baselineWords_);
        cached_ = QJsonDocument(data).toJson(QJsonDocument::Compact);
    }
    return cached_;
}

void DailyCount::setTotalWords(qint64 words)
{
    if (totalWords_ == words) return;
    totalWords_ = words;
    cached_.clear(); // The crumb layout and its fingerprint are still valid.
}

QString DailyCount::publish(const QString &manuscript, const QByteArray &snapshot)
{
    const QString file = statePath(manuscript);
    if (file.isEmpty() || snapshot.isEmpty()) return {};
    const bool active = file == stateFile_;
    if (active && snapshot == committed_) {
        lastDraftPath_.clear();
        return {};
    }
    if (!active && file == lastDraftPath_ && snapshot == lastDraftCheckpoint_) return {};
    const auto data = QJsonDocument::fromJson(snapshot).object();
    const QDate captured = QDate::fromString(data.value("date").toString(), Qt::ISODate);
    if (!captured.isValid()) return {};
    // A serialized disk write may finish after midnight. Every row from
    // that snapshot was already present before the new day began.
    const QByteArray checkpoint = captured == date_ ? snapshot : beforeNewDay(snapshot, date_);
    const QString error = writeState(file, checkpoint);
    if (error.isEmpty()) {
        if (active) {
            committed_ = checkpoint;
            lastDraftPath_.clear(); // DocumentStore removes its recovery draft after a manuscript save.
        }
        else { lastDraftPath_ = file; lastDraftCheckpoint_ = checkpoint; }
    }
    return error;
}

int DailyCount::availableOrigin(quint64 number) const
{
    const auto matches = originsByNumber_.constFind(number);
    if (matches != originsByNumber_.cend())
        for (int id : matches.value())
            if (!occupied_[id]) return id;
    return -1;
}

// An old crumb keeps its identity when its header is corrected. Keep both
// numbers in the baseline so reopening an earlier saved revision or undoing
// that correction still recognizes it. Only one live crumb can use the slot.
bool DailyCount::addAlias(int origin, quint64 number)
{
    if (number == preferred_.value(origin, baseline_[origin])) return false;
    preferred_[origin] = number;
    if (number != baseline_[origin]) {
        auto &names = aliases_[origin];
        if (!names.contains(number)) {
            names.append(number);
            originsByNumber_[number].append(origin);
        }
    }
    return true;
}

bool DailyCount::matchOpened(int trimmedTrailingRows)
{
    indexOrigins();
    // An exact saved layout distinguishes identical old and new headers. An
    // externally modified layout can still be matched against the day's baseline.
    QSet<int> savedNew;
    bool exact = false;
    if (!committed_.isEmpty()) {
        const auto data = QJsonDocument::fromJson(committed_).object();
        const auto lines = data.value("new").toArray();
        const auto expected = data.value("shape").toString().toLatin1();
        const auto extent = headerExtent();
        const auto oldExtent = rows_.size() + trimmedTrailingRows;
        const bool sameShape = expected == shape(extent) ||
            (data.value("shapeFormat").toInt() < 2 && oldExtent != extent && expected == shape(oldExtent));
        if (data.value("date").toString() == date_.toString(Qt::ISODate) &&
            sameShape &&
            data.value("new").isArray() && data.value("count").toInt(-1) == lines.size()) {
            for (const auto &entry : lines) {
                const int line = entry.toInt();
                if (line < 1 || line > rows_.size() || !rows_[line-1].number) { savedNew.clear(); break; }
                savedNew.insert(line - 1);
            }
            exact = savedNew.size() == lines.size();
            if (!exact) savedNew.clear();
        }
    }
    QVector<int> holder(baseline_.size(), -1);
    QHash<quint64, QVector<int>> preferred;
    for (int id = int(baseline_.size()) - 1; id >= 0; --id)
        preferred[preferred_.value(id, baseline_[id])].append(id);
    for (int i = 0; i < rows_.size(); ++i) {
        auto &row = rows_[i];
        if (savedNew.contains(i)) continue;
        auto best = preferred.find(row.number);
        if (best == preferred.end() || best->isEmpty()) continue;
        row.origin = best->takeLast();
        occupied_[row.origin] = true;
        holder[row.origin] = i;
    }
    currentCount_ = 0;
    QSet<quint64> exhausted;
    for (int i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        if (!row.number || row.origin >= 0) continue;
        // A discarded correction or a header-number swap may leave several
        // plausible older identities. Restore a one-to-one assignment.
        if (savedNew.contains(i) || aliases_.isEmpty() || exhausted.contains(row.number)) { ++currentCount_; continue; }
        QVector<int> queue{i};
        QSet<quint64> visitedNumbers;
        QHash<int, int> reachedFrom;
        int free = -1;
        for (int head = 0; head < queue.size() && free < 0; ++head) {
            const int line = queue[head];
            const quint64 number = rows_[line].number;
            if (visitedNumbers.contains(number)) continue;
            visitedNumbers.insert(number);
            const auto options = originsByNumber_.constFind(number);
            if (options == originsByNumber_.cend()) continue;
            for (int id : options.value()) {
                if (reachedFrom.contains(id)) continue;
                reachedFrom.insert(id, line);
                if (holder[id] < 0) { free = id; break; }
                queue.append(holder[id]);
            }
        }
        if (free < 0) {
            exhausted.insert(row.number);
            ++currentCount_;
        } else {
            while (free >= 0) {
                const int line = reachedFrom[free];
                const int previous = rows_[line].origin;
                rows_[line].origin = free;
                occupied_[free] = true;
                holder[free] = line;
                free = previous;
            }
        }
    }
    bool extended = false;
    if (exact) {
        // The midnight baseline uses the open editor. A previous day's
        // unsaved deletion can make the last disk revision contain older
        // records absent from that editor. Discard restores those identities.
        for (int i = 0; i < rows_.size(); ++i) {
            auto &row = rows_[i];
            if (!row.number || row.origin >= 0 || savedNew.contains(i)) continue;
            row.origin = int(baseline_.size());
            baseline_.append(row.number);
            originsByNumber_[row.number].append(row.origin);
            occupied_.append(true);
            --currentCount_;
            extended = true;
        }
    }
    return extended;
}

QString DailyCount::writeState() const
{
    return writeState(stateFile_, committed_);
}

QString DailyCount::writeState(const QString &name, const QByteArray &checkpoint) const
{
    if (name.isEmpty()) return {};
    QJsonArray headers, renamed;
    // Decimal strings also preserve unsigned header numbers above 2^53.
    for (quint64 number : baseline_) headers.append(QString::number(number));
    for (auto it = aliases_.cbegin(); it != aliases_.cend(); ++it) {
        QJsonArray names;
        for (quint64 number : it.value()) names.append(QString::number(number));
        renamed.append(QJsonArray{it.key(), QString::number(preferred_.value(it.key())), names});
    }
    QJsonObject state{{"version", 1}, {"date", date_.toString(Qt::ISODate)}, {"baselineWords", baselineWords_},
                      {"headers", headers}, {"renamed", renamed}};
    auto writeSetting = [&](const QString &name, const Adjustment &setting) {
        if (setting.value || setting.measured)
            state.insert(name, QJsonObject{{"value", setting.value}, {"measured", setting.measured}});
    };
    writeSetting("crumbSetting", crumbSetting_);
    writeSetting("wordSetting", wordSetting_);
    if (!checkpoint.isEmpty()) state.insert("checkpoint", QJsonDocument::fromJson(checkpoint).object());
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    QSaveFile file(name);
    if (!QDir().mkpath(QFileInfo(name).absolutePath()) ||
        !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not save the daily writing counts in %1.").arg(name);
    return {};
}

bool DailyCount::readState(const QByteArray &bytes, QDate date)
{
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (root.value("version").toInt() != 1 || root.value("date").toString() != date.toString(Qt::ISODate) ||
        !root.value("headers").isArray() || !root.value("renamed").isArray())
        return false;
    QVector<quint64> baseline;
    QHash<int, QVector<quint64>> aliases;
    QHash<int, quint64> preferred;
    const auto headers = root.value("headers").toArray();
    baseline.reserve(headers.size());
    for (const auto &entry : headers) {
        const quint64 number = entry.toString().toULongLong();
        if (!number) return false;
        baseline.append(number);
    }
    for (const auto &entry : root.value("renamed").toArray()) {
        const auto pair = entry.toArray();
        if (pair.size() != 3 || !pair[2].isArray()) return false;
        const int id = pair[0].toInt(-1);
        const quint64 latest = pair[1].toString().toULongLong();
        if (id < 0 || id >= baseline.size() || !latest) return false;
        for (const auto &element : pair[2].toArray()) {
            const quint64 number = element.toString().toULongLong();
            if (!number) return false;
            aliases[id].append(number);
        }
        if (latest != baseline[id] && !aliases.value(id).contains(latest)) return false;
        preferred[id] = latest;
    }
    baseline_ = std::move(baseline);
    aliases_ = std::move(aliases);
    preferred_ = std::move(preferred);
    auto readSetting = [&](const QString &name) {
        const auto setting = root.value(name).toObject();
        const qint64 measured = setting.value("measured").toInteger();
        if (measured > std::numeric_limits<int>::max() || measured < -std::numeric_limits<int>::max())
            return Adjustment{};
        return Adjustment{setting.value("value").toInteger(), measured};
    };
    crumbSetting_ = readSetting("crumbSetting");
    wordSetting_ = readSetting("wordSetting");
    baselineWords_ = savedWordBaseline(root, totalWords_);
    const auto checkpoint = root.value("checkpoint");
    if (checkpoint.isObject()) {
        const auto snapshot = checkpoint.toObject();
        committed_ = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
        // At midnight the open buffer and the saved revision can differ.
        // Discarding overnight edits restores the saved revision's baseline.
        if (snapshot.value("date").toString() == date.toString(Qt::ISODate) &&
            snapshot.value("words").toInteger(-1) == totalWords_)
            baselineWords_ = savedWordBaseline(snapshot, baselineWords_);
    }
    date_ = date;
    return true;
}

QString DailyCount::open(const QString &manuscript, QDate date, const QVector<quint64> &numbers,
                         qint64 words, int trimmedTrailingRows)
{
    stateFile_ = statePath(manuscript);
    totalWords_ = words;
    baselineWords_ = words;
    cached_.clear();
    cachedLayout_ = {};
    committed_.clear();
    lastDraftPath_.clear();
    rows_.clear();
    rows_.reserve(numbers.size());
    for (quint64 number : numbers) rows_.append({number, -1});
    if (!stateFile_.isEmpty()) {
        QFile file(stateFile_);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly)) {
                takeBaseline(date);
                return QStringLiteral("Could not read the daily writing counts in %1.").arg(stateFile_);
            }
            const QByteArray saved = file.readAll();
            file.close(); // A restored checkpoint may atomically replace this file on Windows.
            if (readState(saved, date)) {
                const bool extended = matchOpened(trimmedTrailingRows);
                const auto restored = snapshot();
                if (!extended && committed_ == restored) return {};
                committed_ = restored;
                return writeState();
            }
        }
    }
    return startDay(date);
}

QString DailyCount::startDay(QDate date)
{
    const auto saved = committed_;
    const auto draftPath = lastDraftPath_;
    const auto draft = lastDraftCheckpoint_;
    takeBaseline(date);
    if (!stateFile_.isEmpty())
        committed_ = saved.isEmpty() ? snapshot() : beforeNewDay(saved, date);
    const QString error = writeState();
    if (draftPath.isEmpty()) return error;
    lastDraftPath_ = draftPath;
    lastDraftCheckpoint_ = beforeNewDay(draft, date);
    const QString draftError = writeState(draftPath, lastDraftCheckpoint_);
    return error.isEmpty() ? draftError : error;
}

QString DailyCount::saveAs(const QString &manuscript)
{
    stateFile_ = statePath(manuscript);
    committed_.clear();
    return writeState();
}

QString DailyCount::replace(int first, int oldEnd, const std::vector<CrumbRecord> &before,
                            const std::vector<CrumbRecord> &parsed, int editStart, int removed, int added)
{
    const int oldSize = oldEnd - first;
    const int size = int(parsed.size());
    if (oldSize == size && std::equal(parsed.begin(), parsed.end(), rows_.cbegin() + first,
        [](const CrumbRecord &r, const Row &row) {
            return r.number == row.number && !(row.origin >= 0 && r.text.isEmpty());
        }))
        return {}; // Body text, punctuation and formatting need no bookkeeping.

    cached_.clear();
    cachedLayout_ = {};
    QVector<Row> next;
    next.reserve(size);
    for (const auto &r : parsed) next.append({r.number, -1});
    QVector<bool> used(oldSize), matched(size);
    auto carry = [&](int old, int now) {
        next[now].origin = rows_[first + old].origin;
        used[old] = true;
        matched[now] = true;
    };
    if (oldSize != size) {
        // Qt includes the intact bordering row in a split or merge. Its edit
        // offsets distinguish even two adjacent, byte-for-byte identical crumbs.
        QHash<qint64, int> starts;
        qint64 pos = 0;
        for (int i = 0; i < size; ++i) {
            starts.insert(pos, i);
            pos += parsed[i].text.size() + 1;
        }
        pos = 0;
        for (int i = 0; i < oldSize; ++i) {
            const auto &text = before[first + i].text;
            const qint64 end = pos + text.size();
            const qint64 mapped = pos >= qint64(editStart) + removed ? pos + added - removed
                                : end <= editStart ? pos : -1;
            const auto slot = starts.constFind(mapped);
            if (slot != starts.cend() && !matched[*slot] && text == parsed[*slot].text)
                carry(i, *slot);
            pos = end + 1;
        }
    }
    for (int i = 0; i < oldSize; ++i) {
        const auto &row = rows_[first + i];
        if (row.origin >= 0) occupied_[row.origin] = false;
        else if (row.number) --currentCount_;
        if (i < size && !used[i] && !matched[i] && row.number && row.number == next[i].number) carry(i, i);
    }

    // When a newline is inserted above a header, its physical line changes
    // before Qt delivers the new block range. Match surviving numbers first.
    QHash<quint64, QVector<int>> byNumber;
    for (int i = oldSize - 1; i >= 0; --i)
        if (!used[i] && rows_[first + i].number)
            byNumber[rows_[first + i].number].append(i);
    for (int i = 0; i < size; ++i) {
        if (matched[i] || !next[i].number) continue;
        auto old = byNumber.find(next[i].number);
        if (old != byNumber.end() && !old->isEmpty()) carry(old->takeLast(), i);
    }
    if (oldSize == size) {
        for (int i = 0; i < size; ++i) {
            const auto &old = rows_[first + i];
            if (!used[i] && !matched[i] && (old.origin >= 0 || old.number) && !parsed[i].text.isEmpty())
                carry(i, i); // Includes a temporarily incomplete edited header.
        }
    }
    for (const auto &row : std::as_const(next))
        if (row.origin >= 0) occupied_[row.origin] = true;
    bool renamed = false;
    for (int i = 0; i < size; ++i) {
        auto &row = next[i];
        if (!row.number) continue;
        if (!matched[i]) {
            row.origin = availableOrigin(row.number);
            if (row.origin >= 0) occupied_[row.origin] = true;
        }
        if (row.origin < 0) ++currentCount_;
        else if (addAlias(row.origin, row.number)) renamed = true;
    }
    if (oldSize == size) {
        std::copy(next.cbegin(), next.cend(), rows_.begin() + first);
    } else {
        rows_.remove(first, oldSize);
        rows_.insert(first, size, Row{});
        std::copy(next.cbegin(), next.cend(), rows_.begin() + first);
    }
    return renamed ? writeState() : QString();
}
