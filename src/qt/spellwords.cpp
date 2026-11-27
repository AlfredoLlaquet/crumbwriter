#include "spellwords.h"
#include "manuscriptlinks.h"
#include "manuscriptcase.h"

#include <QCoreApplication>
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

QString packagedDictionaryDirectory()
{
#ifdef Q_OS_MACOS
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../Resources/dictionaries");
#elif defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + "/dictionaries";
#else
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../share/crumbwriter/dictionaries");
#endif
}

QByteArray hunspellFilename(const QString &path)
{
#ifdef Q_OS_WIN
    // The executable has a UTF-8 active code page, including for Hunspell's
    // narrow filename interface in the UCRT64 build.
    return QDir::toNativeSeparators(path).toUtf8();
#else
    return QFile::encodeName(path);
#endif
}

class AmericanDictionary {
public:
    AmericanDictionary()
    {
        QStringList roots = qEnvironmentVariable("DICPATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
        const QString bundled = packagedDictionaryDirectory();
        if (!bundled.isEmpty()) roots.prepend(bundled);
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
            handle_.reset(Hunspell_create(hunspellFilename(aff).constData(), hunspellFilename(dic).constData()));
            if (!handle_) continue;
            const char *charset = Hunspell_get_dic_encoding(handle_.get());
            encoding_ = charset ? QStringConverter::encodingForName(charset) : std::nullopt;
            if (encoding_ && Hunspell_spell(handle_.get(), "word")) {
                bundled_ = root == bundled;
                break;
            }
            handle_.reset();
        }
    }

    bool available() const { return bool(handle_); }
    bool bundled() const { return bundled_; }

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
    bool bundled_ = false;
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
    return ManuscriptCase::fold(straight(std::move(word)));
}

bool SpellWords::loading()
{
    return loadedDictionary().wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

bool SpellWords::available() { return american().available(); }
bool SpellWords::usingBundledDictionary() { return american().bundled(); }

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
                Phrase phrase;
                phrase.prefix = s.left(part.capturedStart());
                auto pieces = segmentPattern().globalMatch(s);
                int previousEnd = 0;
                while (pieces.hasNext()) {
                    const auto piece = pieces.next();
                    if (!phrase.words.isEmpty())
                        phrase.following.append(s.mid(previousEnd, piece.capturedStart() - previousEnd));
                    phrase.words.append(key(piece.captured()));
                    previousEnd = int(piece.capturedEnd());
                }
                phrase.following.append(s.mid(previousEnd));
                phrases_[phrase.words.first()].append(phrase);
            }
        }
    }
}

QVector<SpellingRange> SpellWords::phraseRanges(const QString &text, int start) const
{
    QVector<SpellingRange> spans;
    if (phrases_.isEmpty()) return spans;
    struct Token { int start, end; QString folded; };
    QVector<Token> tokens;
    auto it = segmentPattern().globalMatch(text, start);
    while (it.hasNext()) {
        const auto part = it.next();
        tokens.append({int(part.capturedStart()), int(part.capturedEnd()), key(part.captured())});
    }
    const QStringView original(text);
    for (int i = 0; i < tokens.size(); ++i) {
        const auto candidates = phrases_.constFind(tokens.at(i).folded);
        if (candidates == phrases_.cend()) continue;
        for (const auto &p : candidates.value()) {
            if (p.words.size() > tokens.size() - i) continue;
            const int begin = tokens[i].start - int(p.prefix.size());
            if (begin < start || wordAt(text, begin - 1) ||
                original.mid(begin, p.prefix.size()) != p.prefix) continue;
            // Compare folded words separately and return offsets in the original.
            // E.g. STRASSE/ Straße differ in UTF-16 length but have the same key.
            int end = begin;
            bool matches = true;
            for (int n = 0; n < p.words.size(); ++n) {
                const auto &token = tokens.at(i + n);
                const auto &separator = p.following.at(n);
                if ((n && (token.start != end || token.folded != p.words.at(n))) ||
                    original.mid(token.end, separator.size()) != separator) {
                    matches = false;
                    break;
                }
                end = token.end + int(separator.size());
            }
            if (matches && !wordAt(text, end)) spans.append({begin, end});
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
    const auto links = ManuscriptLinks::findAll(original);
    int alias = 0, link = 0;
    auto it = wordPattern().globalMatch(text, start);
    // A dictionary may contain hyphenated terms directly. For other compounds
    // check the individual components so that corrections replace one word.
    auto known = [&](const QString &word) {
        return accepted_.contains(key(word)) || american().correct(word);
    };
    auto check = [&](int begin, int end) {
        while (alias < aliases.size() && aliases[alias].end <= begin) ++alias;
        const auto inPhrase = [&](int limit) {
            return alias < aliases.size() && aliases[alias].start <= begin && aliases[alias].end >= limit;
        };
        if (inPhrase(end)) return;
        auto word = text.mid(begin, end-begin);
        static const QRegularExpression letter(QStringLiteral(R"(\p{L})"));
        if (!letter.match(word).hasMatch() || known(word)) return;
        // Check contractions and apostrophized names intact first. A personal
        // word or a complete multiword name also owns its possessive spelling.
        if (word.endsWith(QStringLiteral("'s"), Qt::CaseInsensitive)) {
            word.chop(2);
            end -= 2;
            if (inPhrase(end) || known(word)) return;
        }
        errors.append({begin, end});
    };
    while (it.hasNext()) {
        const auto match = it.next();
        const int begin = int(match.capturedStart()), end = int(match.capturedEnd());
        while (link < links.size() && links[link].end <= begin) ++link;
        if (link < links.size() && links[link].start < end) continue;
        const QString word = match.captured();
        if (!word.contains(u'-')) { check(begin, end); continue; }
        if (known(word) || (word.endsWith(QStringLiteral("'s"), Qt::CaseInsensitive)
                         && known(word.chopped(2)))) continue;
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
