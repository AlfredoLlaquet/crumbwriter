#include "spellwords.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringConverter>
#include <hunspell.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <optional>

namespace {
QString straight(QString s)
{
    return s.replace(QChar(0x2019), u'\'').replace(QChar(0x02bc), u'\'');
}

const QRegularExpression &segmentPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"([\p{L}\p{N}_][\p{L}\p{N}\p{M}_]*)"));
    return pattern;
}

const QRegularExpression &wordPattern()
{
    // Quotation marks on either side of a word are punctuation. An interior
    // apostrophe belongs to the word, including in I'm, isn't and O'Brien.
    static const QRegularExpression pattern(QStringLiteral(
        R"([\p{L}\p{N}_][\p{L}\p{M}\p{N}_]*(?:['-][\p{L}\p{N}_][\p{L}\p{M}\p{N}_]*)*)"));
    return pattern;
}

bool wordAt(const QString &text, int index)
{
    if (index < 0 || index >= text.size()) return false;
    QChar c = text[index];
    char32_t u = c.unicode();
    if (c.isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate())
        u = QChar::surrogateToUcs4(c, text[index + 1]);
    else if (c.isLowSurrogate() && index && text[index - 1].isHighSurrogate())
        u = QChar::surrogateToUcs4(text[index - 1], c);
    return QChar::isLetterOrNumber(u) || QChar::isMark(u) || u == '_';
}

class AmericanDictionary {
public:
    AmericanDictionary()
    {
        QStringList roots = qEnvironmentVariable("DICPATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (const auto &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
            roots << dir + "/hunspell" << dir + "/myspell/dicts";
        }
        roots << "/usr/local/share/hunspell" << "/usr/share/hunspell"
              << "/usr/local/share/myspell" << "/usr/share/myspell/dicts";
        roots.removeDuplicates();
        for (const auto &root : roots) {
            const QString base = QDir(root).filePath("en_US");
            const QString aff = base + ".aff", dic = base + ".dic";
            if (!QFileInfo(aff).isReadable() || !QFileInfo(dic).isReadable()) continue;
            handle_.reset(Hunspell_create(QFile::encodeName(aff).constData(), QFile::encodeName(dic).constData()));
            if (!handle_) continue;
            const char *charset = Hunspell_get_dic_encoding(handle_.get());
            encoding_ = charset ? QStringConverter::encodingForName(charset) : std::nullopt;
            if (encoding_) break;
            handle_.reset();
        }
    }

    bool available() const { return bool(handle_); }

    bool correct(const QString &word)
    {
        auto previous = known_.constFind(word);
        if (previous != known_.cend()) return previous.value();
        if (!handle_) return true;
        QStringEncoder encode(*encoding_);
        const QByteArray bytes = encode(word);
        const bool valid = !encode.hasError() && Hunspell_spell(handle_.get(), bytes.constData());
        if (known_.size() >= 32768) known_.clear();
        known_.insert(word, valid);
        return valid;
    }

    QStringList suggest(const QString &word) const
    {
        if (!handle_) return {};
        QStringEncoder encode(*encoding_);
        const QByteArray bytes = encode(straight(word));
        if (encode.hasError()) return {};
        char **list = nullptr;
        const int count = Hunspell_suggest(handle_.get(), &list, bytes.constData());
        QStringList result;
        for (int i = 0; i < count; ++i) {
            QStringDecoder decode(*encoding_);
            QString suggestion = decode(QByteArrayView(list[i]));
            if (word.contains(QChar(0x2019))) suggestion.replace(u'\'', QChar(0x2019));
            if (!decode.hasError() && !result.contains(suggestion)) result.append(suggestion);
        }
        if (list) Hunspell_free_list(handle_.get(), &list, count);
        return result;
    }
private:
    std::unique_ptr<Hunhandle, decltype(&Hunspell_destroy)> handle_{nullptr, &Hunspell_destroy};
    std::optional<QStringConverter::Encoding> encoding_;
    QHash<QString, bool> known_;
};

const std::shared_future<std::unique_ptr<AmericanDictionary>> &loadedDictionary()
{
    // Loading Hunspell's affix tables can take tens of milliseconds. Only the
    // loader touches the new instance until the completed future publishes it.
    static const auto load = std::async(std::launch::async, [] {
        return std::make_unique<AmericanDictionary>();
    }).share();
    return load;
}

AmericanDictionary &american() { return *loadedDictionary().get(); }
}

QString SpellWords::key(QString word)
{
    return straight(std::move(word)).normalized(QString::NormalizationForm_C).toCaseFolded();
}

bool SpellWords::loading()
{
    return loadedDictionary().wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

bool SpellWords::available() { return american().available(); }

QStringList SpellWords::suggestions(const QString &word) { return american().suggest(word); }

void SpellWords::setExceptions(const QStringList &aliases, const QStringList &dictionary)
{
    accepted_.clear();
    phrases_.clear();
    for (const auto &source : {&aliases, &dictionary}) {
        for (const auto &entry : *source) {
            QString s = straight(entry.trimmed());
            const auto part = segmentPattern().match(s);
            if (!part.hasMatch()) continue;
            // A phrase must appear in full before ANY of its words are exempt.
            const auto whole = wordPattern().match(s);
            if (whole.capturedStart() == 0 && whole.capturedLength() == s.size()) {
                accepted_.insert(key(s));
            } else {
                phrases_[key(part.captured())].append({s, int(part.capturedStart())});
            }
        }
    }
}

QVector<SpellingRange> SpellWords::phraseRanges(const QString &text, int start) const
{
    QVector<SpellingRange> spans;
    if (phrases_.isEmpty()) return spans;
    auto it = segmentPattern().globalMatch(text, start);
    while (it.hasNext()) {
        const auto part = it.next();
        const auto candidates = phrases_.constFind(key(part.captured()));
        if (candidates == phrases_.cend()) continue;
        for (const auto &p : candidates.value()) {
            const int begin = int(part.capturedStart()) - p.firstWordOffset;
            const int end = begin + int(p.text.size());
            if (begin < start || end > text.size() || wordAt(text, begin - 1) || wordAt(text, end)) continue;
            if (QStringView(text).mid(begin, p.text.size()).compare(p.text, Qt::CaseInsensitive) == 0)
                spans.append({begin, end});
        }
    }
    // Preserve both names when two exempt phrases overlap.
    std::sort(spans.begin(), spans.end(), [](auto a, auto b) { return a.start < b.start; });
    QVector<SpellingRange> merged;
    for (auto span : spans) {
        if (!merged.isEmpty() && merged.last().end >= span.start)
            merged.last().end = std::max(merged.last().end, span.end);
        else merged.append(span);
    }
    return merged;
}

QVector<SpellingRange> SpellWords::issues(const QString &original, int start) const
{
    QVector<SpellingRange> errors;
    if (!available()) return errors;
    // Conversion preserves UTF-16 lengths so ranges also locate curly words.
    const QString text = straight(original);
    const auto aliases = phraseRanges(text, start);
    int alias = 0;
    auto it = wordPattern().globalMatch(text, start);
    // A dictionary may contain hyphenated terms directly. For other compounds
    // check the individual components so that corrections replace one word.
    auto check = [&](int begin, int end) {
        while (alias < aliases.size() && aliases[alias].end <= begin) ++alias;
        if (alias < aliases.size() && aliases[alias].start <= begin && aliases[alias].end >= end) return;
        const auto word = text.mid(begin, end-begin);
        static const QRegularExpression letter(QStringLiteral(R"(\p{L})"));
        if (!letter.match(word).hasMatch() || accepted_.contains(key(word)) || american().correct(word)) return;
        errors.append({begin, end});
    };
    while (it.hasNext()) {
        const auto match = it.next();
        const int begin = int(match.capturedStart()), end = int(match.capturedEnd());
        const QString word = match.captured();
        if (!word.contains(u'-')) { check(begin, end); continue; }
        if (accepted_.contains(key(word)) || american().correct(word)) continue;
        int piece = begin;
        for (int i = begin; i <= end; ++i) {
            if (i == end || text.at(i) == u'-') {
                check(piece, i);
                piece = i + 1;
            }
        }
    }
    return errors;
}
