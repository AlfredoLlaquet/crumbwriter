#include "searchengine.h"

#include <QByteArray>
#include <glib.h>
#include <algorithm>

namespace cwsearch {

int Options::flags() const
{
    if (mode == Mode::Number)
        return 2 | (wrap ? 0 : 32);
    return static_cast<int>(mode) | (matchCase ? 4 : 0) | (wholeWords ? 8 : 0) | (wrap ? 16 : 0);
}

bool Options::validFlags(int flags)
{
    return flags == 34 || (flags >= 0 && flags < 32 && (flags & 3) <= 2);
}

Options Options::fromFlags(int flags)
{
    Options options;
    options.mode = static_cast<Mode>(flags & 3);
    options.matchCase = flags & 4;
    options.wholeWords = flags & 8;
    options.wrap = options.mode == Mode::Number ? !(flags & 32) : flags & 16;
    return options;
}

namespace {

bool wordCharacter(const QString &text, int offset)
{
    if (offset < 0 || offset >= text.size())
        return false;
    QChar c = text.at(offset);
    char32_t cp = c.unicode();
    if (c.isLowSurrogate() && offset > 0 && text.at(offset - 1).isHighSurrogate())
        cp = QChar::surrogateToUcs4(text.at(offset - 1), c);
    else if (c.isHighSurrogate() && offset + 1 < text.size() && text.at(offset + 1).isLowSurrogate())
        cp = QChar::surrogateToUcs4(c, text.at(offset + 1));
    const auto category = QChar::category(cp);
    return QChar::isLetterOrNumber(cp) || category == QChar::Punctuation_Connector ||
           category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining;
}

bool wordBoundary(const QString &text, int position)
{
    return wordCharacter(text, position - 1) != wordCharacter(text, position);
}

QString wrapWholeRegex(const QString &pattern)
{
    // PCRE's start directives, such as (*UTF), precede any enclosing group.
    int start = 0;
    while (QStringView(pattern).mid(start).startsWith(u"(*")) {
        int end = start + 2;
        while (end < pattern.size()) {
            const auto c = pattern.at(end).unicode();
            if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '=')
                break;
            ++end;
        }
        if (end == start + 2 || end == pattern.size() || pattern.at(end) != u')')
            break;
        start = end + 1;
    }
    return pattern.left(start) + QStringLiteral("\\b(?:") + pattern.mid(start) + QStringLiteral(")\\b");
}

QString errorText(GError *error)
{
    if (!error)
        return {};
    QString text = QString::fromUtf8(error->message);
    g_error_free(error);
    return text;
}

// Match offsets arrive monotonically in UTF-8 bytes. Convert each byte at
// most once, including for a document containing supplementary-plane text.
class OffsetReader {
public:
    explicit OffsetReader(const QByteArray &text) : bytes(text) {}
    int at(int end)
    {
        while (byte < end) {
            const auto c = static_cast<unsigned char>(bytes[byte++]);
            if ((c & 0xc0) != 0x80)
                units += (c >= 0xf0) ? 2 : 1;
        }
        return units;
    }
private:
    const QByteArray &bytes;
    int byte = 0;
    int units = 0;
};

int commonPrefix(QStringView a, QStringView b)
{
    int n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n])
        ++n;
    if (n && n < a.size() && a[n].isLowSurrogate())
        --n;
    return n;
}

QString excerpt(const QString &text, int at)
{
    constexpr int width = 104;
    int start = std::max(0, at - 36);
    if (start && start < text.size() && text.at(start).isLowSurrogate())
        --start;
    int end = std::min(int(text.size()), start + width);
    if (end < text.size() && text.at(end).isLowSurrogate())
        --end;
    QString result = text.mid(start, end - start);
    result.replace(u'\n', QChar(0x21b5));
    if (start)
        result.prepend(QChar(0x2026));
    if (end < text.size())
        result.append(QChar(0x2026));
    return result;
}

} // namespace

struct Pattern::Compiled {
    QString query;
    Options options;
    std::shared_ptr<GRegex> regex;
    QString compileError;

    Span literal(const QString &text, int origin, bool backwards) const
    {
        if (query.isEmpty())
            return {};
        const auto sensitivity = options.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int from = backwards ? origin - int(query.size()) : origin;
        while (from >= 0 && from <= text.size()) {
            const int at = backwards ? text.lastIndexOf(query, from, sensitivity)
                                     : text.indexOf(query, from, sensitivity);
            if (at < 0)
                break;
            const int end = at + int(query.size());
            if (!options.wholeWords || (wordBoundary(text, at) && wordBoundary(text, end)))
                return {at, end};
            from = backwards ? at - 1 : at + 1;
        }
        return {};
    }

    // Returning false from visit ends the scan early, including a single
    // replacement that needs PCRE context before the reported match (\\K).
    template<class Visitor>
    QString walk(const QString &text, const std::atomic_bool &cancel, Visitor visit) const
    {
        if (query.isEmpty() || options.mode == Mode::Number || cancel.load())
            return {};
        if (!compileError.isEmpty())
            return compileError;
        if (!regex) {
            int offset = 0;
            while (!cancel.load(std::memory_order_relaxed)) {
                Span hit = literal(text, offset, false);
                if (!hit.found() || !visit(hit, nullptr))
                    break;
                offset = hit.end;
            }
            return {};
        }
        const QByteArray bytes = text.toUtf8();
        OffsetReader positions(bytes);
        GMatchInfo *info = nullptr;
        GError *failure = nullptr;
        bool found = g_regex_match_full(regex.get(), bytes.constData(), bytes.size(), 0,
                                        G_REGEX_MATCH_DEFAULT, &info, &failure);
        while (found && !cancel.load(std::memory_order_relaxed)) {
            int start, end;
            if (!g_match_info_fetch_pos(info, 0, &start, &end) || start < 0 || end < start)
                break;
            const Span span{positions.at(start), positions.at(end)};
            if (!visit(span, info))
                break;
            found = g_match_info_next(info, &failure);
        }
        if (info)
            g_match_info_free(info);
        return errorText(failure);
    }
};

Pattern::Pattern(const QString &query, Options options)
{
    auto expression = std::make_shared<Compiled>();
    expression->query = query;
    expression->options = options;
    if (options.mode == Mode::Regex && !query.isEmpty()) {
        const QString source = options.wholeWords ? wrapWholeRegex(query) : query;
        auto flags = static_cast<GRegexCompileFlags>(G_REGEX_MULTILINE | G_REGEX_UNGREEDY |
                       G_REGEX_OPTIMIZE | (options.matchCase ? 0 : G_REGEX_CASELESS));
        GError *failure = nullptr;
        GRegex *regex = g_regex_new(source.toUtf8().constData(), flags, G_REGEX_MATCH_DEFAULT, &failure);
        if (regex)
            expression->regex.reset(regex, g_regex_unref);
        expression->compileError = errorText(failure);
    }
    compiled = std::move(expression);
}

QString Pattern::error() const
{
    return compiled ? compiled->compileError : QString();
}

Span Pattern::nearestLiteral(const QString &text, int origin, bool backwards, bool wrap, bool &wrapped) const
{
    wrapped = false;
    if (!compiled || compiled->options.mode != Mode::Text)
        return {};
    Span span = compiled->literal(text, origin, backwards);
    if (!span.found() && wrap) {
        span = compiled->literal(text, backwards ? text.size() : 0, backwards);
        wrapped = span.found();
    }
    return span;
}

bool Pattern::hasOverlappingPredecessor(const QString &text, Span selected) const
{
    if (!compiled || !selected.found() || !selected.start || compiled->query.size() < 2)
        return false;
    const int from = std::max(0, selected.start - int(compiled->query.size()) + 1);
    // The selected occurrence is already known to match. Starting this
    // bounded distance earlier can only find it or an overlapping predecessor.
    return compiled->literal(text, from, false).start < selected.start;
}

int nearestIndex(const QVector<Span> &matches, int origin, bool backwards, bool wrap, bool &wrapped)
{
    wrapped = false;
    if (matches.isEmpty())
        return -1;
    if (backwards) {
        auto after = std::upper_bound(matches.begin(), matches.end(), origin,
                                     [](int p, Span m) { return p < m.end; });
        if (after != matches.begin())
            return int(after - matches.begin()) - 1;
    } else {
        auto next = std::lower_bound(matches.begin(), matches.end(), origin,
                                     [](Span m, int p) { return m.start < p; });
        if (next != matches.end())
            return int(next - matches.begin());
    }
    wrapped = wrap;
    return wrap ? (backwards ? int(matches.size()) - 1 : 0) : -1;
}

Matches Pattern::collect(const QString &text, const std::atomic_bool &cancel) const
{
    Matches result;
    if (compiled)
        result.error = compiled->walk(text, cancel, [&](Span span, GMatchInfo *) {
            result.spans.append(span);
            return true;
        });
    return result;
}

static QString expansion(GMatchInfo *info, const QString &replacement, const QByteArray &encoded,
                         QString &error)
{
    if (!info)
        return replacement;
    GError *failure = nullptr;
    char *value = g_match_info_expand_references(info, encoded.constData(), &failure);
    error = errorText(failure);
    QString result = QString::fromUtf8(value);
    g_free(value);
    return result;
}

QString Pattern::expandOne(const QString &text, Span selected, const QString &replacement, QString &error) const
{
    error.clear();
    if (!compiled || !selected.found())
        return {};
    if (!compiled->regex)
        return replacement;
    // Even a zero-length match can have a capture in a lookbehind.
    QString result;
    bool found = false;
    const QByteArray encoded = replacement.toUtf8();
    const std::atomic_bool cancel{false};
    QString failure = compiled->walk(text, cancel, [&](Span span, GMatchInfo *info) {
        if (span == selected) {
            found = true;
            result = expansion(info, replacement, encoded, error);
        }
        return !found && span.start <= selected.start;
    });
    if (!failure.isEmpty())
        error = failure;
    else if (!found)
        error = QStringLiteral("The selected match has changed.");
    return result;
}

ReplacementPlan Pattern::plan(const QString &text, const QString &replacement,
                              const std::function<QString(QString)> &normalize,
                              const std::atomic_bool &cancel) const
{
    ReplacementPlan plan;
    if (!compiled)
        return plan;
    const QByteArray encoded = replacement.toUtf8();
    if (compiled->regex) {
        GError *failure = nullptr;
        if (!g_regex_check_replacement(encoded.constData(), nullptr, &failure)) {
            plan.error = errorText(failure);
            return plan;
        }
    }
    QVector<int> lines{0};
    for (int i = text.indexOf(u'\n'); i >= 0; i = text.indexOf(u'\n', i + 1))
        lines.append(i + 1);
    auto lineAt = [&](int at) {
        return int(std::upper_bound(lines.begin(), lines.end(), at) - lines.begin()) - 1;
    };
    int first = -1, last = -1, copied = 0, changes = 0;
    QString output;
    auto flush = [&] {
        if (first < 0)
            return;
        const int begin = lines[first];
        const int end = last + 1 < lines.size() ? lines[last + 1] - 1 : int(text.size());
        output.append(QStringView(text).mid(copied, end - copied));
        QString after = normalize(std::move(output));
        output.clear();
        const auto before = QStringView(text).mid(begin, end - begin);
        if (before != after) {
            const int prefix = commonPrefix(before, after);
            int suffix = 0;
            while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
                   before[before.size() - suffix - 1] == after[after.size() - suffix - 1])
                ++suffix;
            if (suffix && before[before.size() - suffix].isLowSurrogate())
                --suffix;
            const Span changed{begin + prefix, end - suffix};
            plan.edits.append({changed, after.mid(prefix, after.size() - prefix - suffix)});
            plan.changes += changes;
            plan.affectedLines += lineAt(changed.end) - lineAt(changed.start) + 1;
            if (plan.examples.size() < 4)
                plan.examples.append({first + 1, excerpt(before.toString(), prefix), excerpt(after, prefix)});
        }
        first = last = -1;
        changes = 0;
    };
    QString failed = compiled->walk(text, cancel, [&](Span hit, GMatchInfo *info) {
        const QString expanded = expansion(info, replacement, encoded, plan.error);
        if (!plan.error.isEmpty())
            return false;
        if (QStringView(text).mid(hit.start, hit.end - hit.start) == expanded)
            return true;
        const int from = lineAt(hit.start);
        if (from > last)
            flush();
        if (first < 0) {
            first = from;
            copied = lines[first];
        }
        last = lineAt(hit.end);
        output.append(QStringView(text).mid(copied, hit.start - copied));
        output.append(expanded);
        copied = hit.end;
        ++changes;
        return true;
    });
    if (!failed.isEmpty())
        plan.error = failed;
    if (!cancel.load() && plan.error.isEmpty())
        flush();
    return plan;
}

} // namespace cwsearch
