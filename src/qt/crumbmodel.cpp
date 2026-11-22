#include "crumbmodel.h"
#include "dailycount.h"
#include "manuscripttext.h"

#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>

#include <glib.h>
extern "C" {
#include "../crumb.h"
}

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {
constexpr quint64 maximumNumber = std::numeric_limits<quint64>::max();
using Occurrence = std::pair<int, int>; // Physical line, component in its title.

bool isGenreDelimiter(QChar c)
{
    return c == u',' || c == u'(' || c == u')';
}

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

QString genreKey(const QString &name)
{
    // GLib's full case folding retains 1.x equivalences such as Straße/STRASSE.
    const QByteArray utf8 = name.toUtf8();
    char *folded = g_utf8_casefold(utf8.constData(), utf8.size());
    QString key = QString::fromUtf8(folded).normalized(QString::NormalizationForm_C).simplified();
    g_free(folded);
    return key;
}

QString nameBeforeHash(const QString &title, int first, int hash)
{
    if (hash <= first || !title.at(hash - 1).isSpace())
        return {};
    return title.mid(first, hash - first).trimmed();
}

QVector<CrumbGenrePart> parseGenres(const QString &title, int titleStart)
{
    QVector<CrumbGenrePart> result;
    if (!title.contains(u'#'))
        return result;

    const int length = int(title.size());
    for (int first = 0; first < length;) {
        int end = first;
        while (end < length && !isGenreDelimiter(title.at(end)))
            ++end;
        int last = end;
        while (last > first && title.at(last - 1).isSpace())
            --last;
        int digits = last;
        while (digits > first && isDigit(title.at(digits - 1)))
            --digits;
        if (digits < last && digits > first && title.at(digits - 1) == u'#') {
            const QString name = nameBeforeHash(title, first, digits - 1);
            if (!name.isEmpty()) {
                bool ok = false;
                const quint64 number = QStringView(title).mid(digits, last - digits).toULongLong(&ok);
                result.append({genreKey(name), name, ok ? number : maximumNumber,
                               titleStart + digits, titleStart + last, !ok, false});
            }
        }
        first = end + 1;
    }
    return result;
}

bool sameGenres(const CrumbRecord &a, const CrumbRecord &b)
{
    if (a.genres.size() != b.genres.size())
        return false;
    for (int i = 0; i < a.genres.size(); ++i) {
        const auto &left = a.genres.at(i);
        const auto &right = b.genres.at(i);
        if (left.key != right.key || left.name != right.name ||
            left.number != right.number || left.overflow != right.overflow)
            return false;
    }
    return true;
}

// The tested UTF-8 grammar returns Unicode code-point counts. Advancing through
// the prefix once converts the ordered boundaries into QTextCursor columns.
class ColumnMap {
public:
    explicit ColumnMap(const QString &text) : text(text) {}
    int at(int codePoint)
    {
        while (scalar < codePoint && offset < text.size()) {
            const bool pair = text.at(offset).isHighSurrogate() && offset + 1 < text.size() &&
                              text.at(offset + 1).isLowSurrogate();
            offset += pair ? 2 : 1;
            ++scalar;
        }
        return offset;
    }
private:
    const QString &text;
    int scalar = 0;
    int offset = 0;
};

CrumbRecord parseRecord(const QString &text, bool firstLine, const CrumbRecord *old = nullptr)
{
    CrumbRecord record;
    record.text = text;
    record.blank = QStringView(text).trimmed().isEmpty();
    QByteArray bytes = text.toUtf8();
    CwCrumb parsed{};
    cw_crumb_parse(bytes.constData(), firstLine, &parsed);
    ColumnMap map(text);
    record.recordStart = map.at(parsed.record_start);
    record.hasWorkSeparator = parsed.record_start > 0;
    const char *start = g_utf8_offset_to_pointer(bytes.constData(), parsed.record_start);
    record.words = int(cw_crumb_words(start, false));
    record.headerStart = map.at(parsed.header_start);
    record.digitsStart = map.at(parsed.digits_start);
    record.headerEnd = map.at(parsed.header_end);
    if (parsed.valid) {
        record.valid = true;
        record.number = parsed.number;
        record.label = QString::fromUtf8(parsed.label);
        record.titleStart = map.at(parsed.title_start);
        record.titleEnd = map.at(parsed.title_end);
        record.bodyStart = map.at(parsed.body_start);
        record.title = text.mid(record.titleStart, record.titleEnd - record.titleStart);
        if (old && record.title == old->title) {
            record.genres = old->genres;
            const int displacement = record.titleStart - old->titleStart;
            if (displacement) {
                for (auto &part : record.genres) {
                    part.start += displacement;
                    part.end += displacement;
                }
            }
        } else {
            record.genres = parseGenres(record.title, record.titleStart);
        }
    }
    // The official whole-manuscript command splits the first literal // on
    // line 1. A generic document has no work-title stage in its word count.
    if (record.hasWorkSeparator) {
        bytes.replace(bytes.indexOf("//"), 2, " ");
        record.fileWords = int(cw_crumb_words(bytes.constData(), false));
    } else
        record.fileWords = record.words;
    cw_crumb_clear(&parsed);
    return record;
}

bool sameWarnings(const QVector<CrumbWarning> &a, const QVector<CrumbWarning> &b)
{
    return a.size() == b.size() && std::equal(a.cbegin(), a.cend(), b.cbegin(),
        [](const CrumbWarning &x, const CrumbWarning &y) {
            return x.start == y.start && x.end == y.end && x.genre == y.genre &&
                   x.actual == y.actual && x.expected == y.expected;
        });
}

struct Observer {
    QPointer<QObject> owner;
    std::function<void(const CrumbChange &)> update;
};
} // namespace

struct CrumbModel::Data {
    Data(QTextDocument *document, std::function<QDate()> date)
        : document(document), localDate(std::move(date)) {}

    QTextDocument *document;
    std::vector<CrumbRecord> rows;
    std::map<quint64, std::set<int>> numbers;
    std::set<int> recognized;
    std::map<QString, std::set<Occurrence>> genres;
    std::set<int> warningRows;
    std::map<int, QString> titles;
    qint64 fileWords = 0;
    int nonblankParagraphs = 0;
    std::function<QDate()> localDate;
    DailyCount daily;
    QTimer dateTimer;
    QString fallbackLabel = QStringLiteral("Crumb");
    QVector<Observer> observers;

    QString openDaily(const QString &manuscript)
    {
        QVector<quint64> headers;
        headers.reserve(rows.size());
        for (const auto &record : rows) headers.append(record.number);
        return daily.open(manuscript, localDate(), headers, fileWords);
    }

    bool generic() const { return rows.empty() || !rows.front().hasWorkSeparator; }

    // A terminal LF ends a file line. Qt adds an editable empty block after
    // it, but only a second LF can supply a blank separator at that position.
    int physicalLines() const
    {
        return int(rows.size()) - (!rows.empty() && rows.back().text.isEmpty() ? 1 : 0);
    }

    ParagraphRole classify(int line) const
    {
        const auto &row = rows[line];
        if (row.blank) return ParagraphRole::Blank;
        const bool followedByBlank = line + 1 < physicalLines() && rows[line + 1].blank;
        if (line == 0) {
            if (followedByBlank || !row.text.contains(u'.') ||
                (row.text.isUpper() && !row.text.isLower()))
                return ParagraphRole::DocumentTitle;
        } else if (followedByBlank && rows[line - 1].blank) {
            return ParagraphRole::ChapterTitle;
        }
        return ParagraphRole::Body;
    }

    void refreshTitles(CrumbChange &change, int oldEnd, int delta)
    {
        if (change.modeChanged) {
            titles.clear();
            change.firstLine = 0;
            change.lastLine = int(rows.size()) - 1;
            change.titlesChanged = true;
        }
        if (!generic()) return;
        if (delta && !change.modeChanged) {
            // Only indexed headings after the edit need new keys. Paragraph
            // text and roles farther down the manuscript are already current.
            auto first = titles.lower_bound(change.firstLine);
            if (first != titles.end()) {
                std::map<int, QString> shifted;
                for (auto it = titles.lower_bound(oldEnd); it != titles.end(); ++it)
                    shifted.emplace(it->first + delta, it->second);
                titles.erase(first, titles.end());
                titles.merge(shifted);
                change.titlesChanged = true;
            }
        }
        const int first = std::max(0, change.firstLine - 1);
        const int last = std::min(int(rows.size()) - 1, change.lastLine + 1);
        for (int i = first; i <= last; ++i) {
            auto &row = rows[i];
            const auto role = classify(i);
            if (row.role != role) {
                row.role = role;
                change.firstLine = std::min(change.firstLine, i);
                change.lastLine = std::max(change.lastLine, i);
            }
            auto title = titles.find(i);
            if (role == ParagraphRole::DocumentTitle || role == ParagraphRole::ChapterTitle) {
                if (title == titles.end() || title->second != row.text) {
                    titles.insert_or_assign(i, row.text);
                    change.titlesChanged = true;
                }
            } else if (title != titles.end()) {
                titles.erase(title);
                change.titlesChanged = true;
            }
        }
    }

    int headerPosition(int line) const
    {
        return document->findBlockByNumber(line).position() + rows.at(line).headerStart;
    }

    void addNumber(int line, const CrumbRecord &r)
    {
        if (!r.valid)
            return;
        numbers[r.number].insert(line);
        recognized.insert(line);
    }

    void removeNumber(int line, const CrumbRecord &r)
    {
        if (!r.valid)
            return;
        auto it = numbers.find(r.number);
        it->second.erase(line);
        if (it->second.empty()) numbers.erase(it);
        recognized.erase(line);
    }

    // Only the immediate successor can acquire a different predecessor.
    void removeParts(int line, const CrumbRecord &r, std::set<int> &check)
    {
        for (int i = 0; i < r.genres.size(); ++i) {
            auto genre = genres.find(r.genres.at(i).key);
            auto &entries = genre->second;
            const auto successor = entries.erase(entries.find({line, i}));
            if (successor != entries.end())
                check.insert(successor->first);
            if (entries.empty())
                genres.erase(genre);
        }
    }

    void addParts(int line, const CrumbRecord &r, std::set<int> *check = nullptr)
    {
        for (int i = 0; i < r.genres.size(); ++i) {
            auto &entries = genres[r.genres.at(i).key];
            const auto inserted = entries.insert({line, i}).first;
            const auto successor = std::next(inserted);
            if (check && successor != entries.end())
                check->insert(successor->first);
        }
    }

    bool checkWarnings(int line)
    {
        auto &r = rows.at(line);
        auto previous = std::move(r.warnings);
        r.warnings.clear();
        r.headerWarning = r.headerEnd > r.digitsStart &&
                          (!r.valid || r.number != quint64(line) + 1);
        auto warn = [&](int start, int end, const QString &expected, const QString &genre) {
            r.warnings.append({line, start, end, r.text.mid(start, end - start), expected, genre});
        };
        if (r.headerWarning)
            warn(r.digitsStart, r.headerEnd, QString::number(quint64(line) + 1), {});
        for (int i = 0; i < r.genres.size(); ++i) {
            auto &part = r.genres[i];
            const auto &entries = genres.at(part.key);
            const auto here = entries.find({line, i});
            quint64 prior = 0;
            if (here != entries.begin()) {
                const auto [priorLine, priorPart] = *std::prev(here);
                prior = rows.at(priorLine).genres.at(priorPart).number;
            }
            const bool exhausted = prior == maximumNumber;
            part.warning = part.overflow || exhausted || part.number != prior + 1;
            if (part.warning)
                warn(part.start, part.end, exhausted ? QStringLiteral("no higher number")
                                                     : QString::number(prior + 1), part.name);
        }
        if (r.warnings.isEmpty())
            warningRows.erase(line);
        else
            warningRows.insert(line);
        return !sameWarnings(previous, r.warnings);
    }

    void rebuildIndices(CrumbChange &change)
    {
        numbers.clear();
        recognized.clear();
        genres.clear();
        warningRows.clear();
        for (int line = 0; line < int(rows.size()); ++line) {
            addNumber(line, rows[line]);
            addParts(line, rows[line]);
        }
        for (int line = 0; line < int(rows.size()); ++line) {
            if (checkWarnings(line) && (line < change.firstLine || line > change.lastLine))
                change.otherLines.append(line);
        }
        change.genresChanged = true;
    }

    void replaceRows(int first, int oldEnd, std::vector<CrumbRecord> &parsed)
    {
        if (int(parsed.size()) == oldEnd - first) {
            std::move(parsed.begin(), parsed.end(), rows.begin() + first);
        } else {
            auto place = rows.erase(rows.begin() + first, rows.begin() + oldEnd);
            rows.insert(place, std::make_move_iterator(parsed.begin()),
                        std::make_move_iterator(parsed.end()));
        }
    }

    void updateLocal(CrumbChange &change, int oldEnd, std::vector<CrumbRecord> &parsed)
    {
        std::set<int> check;
        const int first = change.firstLine;
        const int last = change.lastLine;
        const int end = std::max(oldEnd, last + 1);
        for (int line = first; line < end; ++line) {
            const auto *old = line < oldEnd ? &rows[line] : nullptr;
            const auto *current = line <= last ? &parsed[line - first] : nullptr;
            if (!old || !current || old->valid != current->valid || old->number != current->number) {
                if (old)
                    removeNumber(line, *old);
                if (current)
                    addNumber(line, *current);
            }
            if (!old || !current || !sameGenres(*old, *current)) {
                if (old)
                    removeParts(line, *old, check);
                if (current)
                    addParts(line, *current, &check);
                if ((old && !old->genres.isEmpty()) || (current && !current->genres.isEmpty()))
                    change.genresChanged = true;
            }
            if (current)
                check.insert(line);
            else
                warningRows.erase(line);
        }
        replaceRows(first, oldEnd, parsed);
        for (int line : check) {
            if (line >= int(rows.size()))
                continue;
            if (checkWarnings(line) && (line < first || line > last))
                change.otherLines.append(line);
        }
    }
};

CrumbModel::CrumbModel(QTextDocument *document, std::function<QDate()> localDate)
    : d(std::make_unique<Data>(document, std::move(localDate)))
{
    // The detailed contentsChange signal is emitted for documents with a layout.
    document->documentLayout();
    CrumbChange initial;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        d->rows.push_back(parseRecord(ManuscriptText::blockText(block), block.blockNumber() == 0));
        d->fileWords += d->rows.back().fileWords;
        if (!d->rows.back().blank) ++d->nonblankParagraphs;
    }
    initial.lastLine = int(d->rows.size()) - 1;
    d->refreshTitles(initial, 0, 0);
    d->rebuildIndices(initial);
    markOpened();
    connect(document, &QTextDocument::contentsChange, this, &CrumbModel::update);
    d->dateTimer.setSingleShot(true);
    auto schedule = [this] {
        const auto now = QDateTime::currentDateTime();
        const auto midnight = now.date().addDays(1).startOfDay();
        // Also pick up a clock or time-zone change while the window is idle.
        d->dateTimer.start(int(std::clamp<qint64>(now.msecsTo(midnight), 1, 60000)));
    };
    connect(&d->dateTimer, &QTimer::timeout, this, [this, schedule] { refreshDay(); schedule(); });
    schedule();
}

CrumbModel::~CrumbModel() = default;

void CrumbModel::update(int position, int removed, int added)
{
    if (removed == 0 && added == 0)
        return;
    auto *doc = d->document;
    const int count = doc->blockCount();
    const int oldCount = int(d->rows.size());
    const bool wasGeneric = d->generic();
    const int delta = count - oldCount;
    const int lastPosition = doc->characterCount() - 1;
    int first = doc->findBlock(std::clamp(position, 0, lastPosition)).blockNumber();
    const int rangeEnd = int(std::clamp<qint64>(qint64(position) + added, 0, lastPosition));
    int last = doc->findBlock(rangeEnd).blockNumber();
    int oldEnd = last + 1 - delta;
    if (first < 0 || first > oldCount || last < first || oldEnd < first || oldEnd > oldCount) {
        // A custom document layout can report replacement of the entire buffer.
        first = 0;
        last = count - 1;
        oldEnd = oldCount;
    }
    std::vector<CrumbRecord> parsed;
    parsed.reserve(last + 1 - first);
    auto block = doc->findBlockByNumber(first);
    const int editStart = position - block.position();
    bool changed = delta != 0;
    for (int line = first; line <= last; ++line, block = block.next()) {
        const QString text = ManuscriptText::blockText(block);
        const auto *old = line < oldEnd ? &d->rows[line] : nullptr;
        if (old && old->text == text) {
            parsed.push_back(*old);
        } else {
            changed = true;
            parsed.push_back(parseRecord(text, line == 0, old));
        }
    }
    if (!changed)
        return;

    // The first edit after midnight can precede the queued timer event.
    // The unedited model still contains yesterday's closing baseline here.
    const QDate today = d->localDate();
    if (d->daily.date() != today) {
        const QString error = d->daily.startDay(today);
        if (!error.isEmpty() && onDailyCountError) onDailyCountError(error);
    }
    const QString dailyError = d->daily.replace(first, oldEnd, d->rows, parsed, editStart, removed, added);
    if (!dailyError.isEmpty() && onDailyCountError) onDailyCountError(dailyError);
    for (int line = first; line < oldEnd; ++line) {
        d->fileWords -= d->rows[line].fileWords;
        if (!d->rows[line].blank) --d->nonblankParagraphs;
    }
    for (const auto &record : parsed) {
        d->fileWords += record.fileWords;
        if (!record.blank) ++d->nonblankParagraphs;
    }
    d->daily.setTotalWords(d->fileWords);

    CrumbChange change;
    change.firstLine = first;
    change.lastLine = last;
    change.structureChanged = delta != 0;
    change.textChanged = true;
    const bool indexedSuffixMoves = delta &&
        (d->recognized.lower_bound(oldEnd) != d->recognized.end() ||
         d->warningRows.lower_bound(oldEnd) != d->warningRows.end());
    if (indexedSuffixMoves) {
        // Line keys to the right shifted. Keep all unaffected parsed records,
        // then rebuild their integer keys once for the entire edit group.
        d->replaceRows(first, oldEnd, parsed);
        d->rebuildIndices(change);
    } else {
        // Appending before an existing final LF shifts only an unindexed blank
        // row. Ordinary new crumbs should cost the same with either EOF style.
        d->updateLocal(change, oldEnd, parsed);
    }
    if (!d->recognized.empty())
        d->fallbackLabel = d->rows[*d->recognized.rbegin()].label;
    change.modeChanged = wasGeneric != d->generic();
    d->refreshTitles(change, oldEnd, delta);
    notify(change);
}

const CrumbRecord &CrumbModel::record(int line) const
{
    static const CrumbRecord empty;
    return line >= 0 && line < int(d->rows.size()) ? d->rows[line] : empty;
}

QVector<CrumbGenre> CrumbModel::genres() const
{
    QVector<CrumbGenre> result;
    result.reserve(d->genres.size());
    for (const auto &[key, occurrences] : d->genres) {
        const auto [line, component] = *occurrences.rbegin();
        const auto &part = d->rows[line].genres.at(component);
        result.append({key, part.name, part.number, part.warning, part.number < maximumNumber});
    }
    return result;
}

int CrumbModel::crumbCount() const { return int(d->recognized.size()); }
bool CrumbModel::isGeneric() const { return d->generic(); }
int CrumbModel::paragraphCount() const { return d->nonblankParagraphs; }
ParagraphRole CrumbModel::paragraphRole(int line) const
{
    return isGeneric() ? record(line).role : ParagraphRole::Body;
}

QVector<int> CrumbModel::titleLines() const
{
    QVector<int> lines;
    lines.reserve(d->titles.size());
    for (const auto &entry : d->titles) lines.append(entry.first);
    return lines;
}

int CrumbModel::neighborTitle(int line, bool backward) const
{
    if (backward) {
        auto it = d->titles.lower_bound(line);
        return it == d->titles.begin() ? -1 : std::prev(it)->first;
    }
    auto it = d->titles.upper_bound(line);
    return it == d->titles.end() ? -1 : it->first;
}

qint64 CrumbModel::totalWords() const { return d->fileWords; }
int CrumbModel::todayCount() const { return d->daily.count(); }
qint64 CrumbModel::todayWords() const { return d->daily.wordChange(); }

void CrumbModel::markOpened()
{
    d->openDaily({});
    d->fallbackLabel = d->recognized.empty() ? QStringLiteral("Crumb")
                                             : d->rows[*d->recognized.rbegin()].label;
    CrumbChange change;
    change.lastLine = std::max(0, int(d->rows.size()) - 1);
    change.genresChanged = true;
    change.titlesChanged = true;
    notify(change);
}

QString CrumbModel::openDailyCount(const QString &manuscript)
{
    const int previous = todayCount();
    const qint64 previousWords = todayWords();
    const auto error = d->openDaily(manuscript);
    if (previous != todayCount() || previousWords != todayWords()) notify({});
    return error;
}

QString CrumbModel::saveDailyCountAs(const QString &manuscript)
{
    refreshDay();
    return d->daily.saveAs(manuscript);
}

QByteArray CrumbModel::dailySnapshot()
{
    refreshDay();
    return d->daily.snapshot();
}

QString CrumbModel::publishDailySnapshot(const QString &manuscript, const QByteArray &snapshot)
{
    refreshDay();
    return d->daily.publish(manuscript, snapshot);
}

void CrumbModel::refreshDay()
{
    const QDate date = d->localDate();
    if (d->daily.date() == date) return;
    const QString error = d->daily.startDay(date);
    if (!error.isEmpty() && onDailyCountError) onDailyCountError(error);
    notify({});
}

int CrumbModel::findNumber(quint64 number, int origin, bool backward, bool wrap,
                           bool includeOrigin) const
{
    const auto source = d->numbers.find(number);
    if (source == d->numbers.end())
        return -1;
    const auto &matches = source->second;
    const int position = std::clamp(origin, 0, d->document->characterCount() - 1);
    const int line = d->document->findBlock(position).blockNumber();
    if (backward) {
        auto it = matches.upper_bound(line);
        while (it != matches.begin()) {
            const int match = d->headerPosition(*--it);
            if (match < origin || (includeOrigin && match == origin))
                return match;
        }
        return wrap ? d->headerPosition(*matches.rbegin()) : -1;
    }
    auto it = matches.lower_bound(line);
    while (it != matches.end()) {
        const int match = d->headerPosition(*it++);
        if (match > origin || (includeOrigin && match == origin))
            return match;
    }
    return wrap ? d->headerPosition(*matches.begin()) : -1;
}

int CrumbModel::neighbor(int line, bool backward) const
{
    if (isGeneric()) {
        const int count = d->physicalLines();
        if (count == 0) return -1;
        if (backward) return line > 0 ? std::min(count, line) - 1 : -1;
        return line < count - 1 ? std::max(0, line + 1) : -1;
    }
    if (backward) {
        const auto it = d->recognized.lower_bound(line);
        return it == d->recognized.begin() ? -1 : *std::prev(it);
    }
    const auto it = d->recognized.upper_bound(line);
    return it == d->recognized.end() ? -1 : *it;
}

QString CrumbModel::nextHeader(const QString &title) const
{
    const quint64 largest = d->numbers.empty() ? 0 : d->numbers.rbegin()->first;
    if (largest == maximumNumber)
        return {};
    return d->fallbackLabel + u' ' + QString::number(largest + 1) +
           QStringLiteral(": ") + title + QStringLiteral(": ");
}

QString CrumbModel::nextGenreTitle(const QString &name) const
{
    const auto it = d->genres.find(genreKey(name));
    if (it == d->genres.end())
        return {};
    const auto [line, component] = *it->second.rbegin();
    const auto &part = d->rows[line].genres.at(component);
    return part.number < maximumNumber ? part.name + QStringLiteral(" #") + QString::number(part.number + 1)
                                       : QString();
}

QString CrumbModel::completeGenre(int line, int hashColumn) const
{
    const auto block = d->document->findBlockByNumber(line);
    if (!block.isValid())
        return {};
    // Input methods may deliver # inside a still-open QTextCursor edit block.
    const CrumbRecord current = parseRecord(ManuscriptText::blockText(block), line == 0);
    const int hash = hashColumn - current.titleStart;
    const auto &title = current.title;
    if (!current.valid || hash < 0 || hash >= title.size() || title.at(hash) != u'#')
        return {};
    int after = hash + 1;
    while (after < title.size() && title.at(after).isSpace())
        ++after;
    if (after < title.size() && title.at(after) != u',' && title.at(after) != u')')
        return {};
    int first = hash;
    while (first > 0 && !isGenreDelimiter(title.at(first - 1)))
        --first;
    const QString name = nameBeforeHash(title, first, hash);
    const auto it = d->genres.find(genreKey(name));
    if (it == d->genres.end())
        return {};
    const auto &entries = it->second;
    const auto next = entries.lower_bound({line, 0});
    if (next == entries.begin())
        return {};
    const auto [beforeLine, component] = *std::prev(next);
    const quint64 prior = d->rows[beforeLine].genres.at(component).number;
    return prior < maximumNumber ? QString::number(prior + 1) : QString();
}

std::optional<CrumbWarning> CrumbModel::nextWarning(int origin, bool backward, bool wrap) const
{
    const auto &rows = d->warningRows;
    if (rows.empty())
        return {};
    const int position = std::clamp(origin, 0, d->document->characterCount() - 1);
    const int line = d->document->findBlock(position).blockNumber();
    if (backward) {
        auto it = rows.upper_bound(line);
        while (it != rows.begin()) {
            const int candidate = *--it;
            const int begin = d->document->findBlockByNumber(candidate).position();
            const auto &ranges = d->rows[candidate].warnings;
            for (auto warning = ranges.crbegin(); warning != ranges.crend(); ++warning)
                if (begin + warning->start < origin)
                    return *warning;
        }
        if (wrap)
            return d->rows[*rows.rbegin()].warnings.last();
    } else {
        for (auto it = rows.lower_bound(line); it != rows.end(); ++it) {
            const int begin = d->document->findBlockByNumber(*it).position();
            for (const auto &warning : std::as_const(d->rows[*it].warnings))
                if (begin + warning.start > origin)
                    return warning;
        }
        if (wrap)
            return d->rows[*rows.begin()].warnings.first();
    }
    return {};
}

void CrumbModel::observe(QObject *owner, std::function<void(const CrumbChange &)> handler)
{
    d->observers.append({owner, std::move(handler)});
}

void CrumbModel::notify(const CrumbChange &change)
{
    d->observers.removeIf([](const Observer &observer) { return observer.owner.isNull(); });
    const auto observers = d->observers;
    for (const auto &observer : observers)
        if (observer.owner)
            observer.update(change);
}
