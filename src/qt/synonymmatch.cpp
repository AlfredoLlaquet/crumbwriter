#include "synonymmatch.h"
#include "manuscripttext.h"

#include <QHash>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>

namespace {
char32_t scalarAt(QStringView text, int pos)
{
    const QChar c = text.at(pos);
    if (c.isHighSurrogate() && pos + 1 < text.size() && text.at(pos + 1).isLowSurrogate())
        return QChar::surrogateToUcs4(c, text.at(pos + 1));
    return c.unicode();
}

int width(char32_t c) { return QChar::requiresSurrogates(c) ? 2 : 1; }

char32_t folded(char32_t c) { return c == 0x2019 ? U'\'' : QChar::toCaseFolded(c); }

bool word(char32_t c) { return c == U'_' || QChar::isLetterOrNumber(c) || QChar::isMark(c); }

struct PrefixNode {
    QHash<char32_t, int> next;
    QList<int> groups; // Groups whose complete alias ends here.
    QString characterAlias, recurringAlias;
    bool hasCharacters = false, hasRecurring = false; // Includes descendants.
};

struct BlockMentions final : public QTextBlockUserData {
    QPointer<const SynonymMatcher> owner;
    quint64 generation = 0;
    int revision = 0;
    QList<SynonymMention> relative;
};
}

struct SynonymMatcher::Data {
    QPointer<QTextDocument> document;
    QList<SynonymGroup> groups;
    QList<PrefixNode> nodes{PrefixNode{}};
    int longest = 0; // Maximum alias lengths in Unicode scalar values.
    int longestCompletable = 0;
    quint64 generation = 0;

    void addGroup(const QStringList &aliases, NameSource source, bool complete)
    {
        const int group = int(groups.size());
        groups.append({aliases, source});
        const bool recurring = source == NameSource::Recurring;
        for (const QString &alias : aliases) {
            if (alias.isEmpty()) continue;
            int node = 0, length = 0;
            for (int pos = 0; pos < alias.size(); ++length) {
                const auto c = scalarAt(alias, pos);
                pos += width(c);
                const auto key = folded(c);
                int child = nodes.at(node).next.value(key, -1);
                if (child < 0) {
                    child = int(nodes.size());
                    nodes[node].next.insert(key, child);
                    nodes.append(PrefixNode{});
                }
                node = child;
                if (complete) {
                    if (recurring) nodes[node].hasRecurring = true;
                    else nodes[node].hasCharacters = true;
                }
            }
            auto &terminal = nodes[node];
            if (terminal.groups.isEmpty() || terminal.groups.last() != group)
                terminal.groups.append(group);
            if (complete) {
                auto &spelling = recurring ? terminal.recurringAlias : terminal.characterAlias;
                if (spelling.isEmpty()) spelling = alias;
            }
            longest = std::max(longest, length);
            if (complete) longestCompletable = std::max(longestCompletable, length);
        }
    }

    QList<SynonymMention> scan(QStringView text) const
    {
        QList<SynonymMention> hits;
        if (!longest) return hits;
        bool boundary = true;
        int coveredUntil = 0;
        for (int start = 0; start < text.size();) {
            const auto first = scalarAt(text, start);
            const int after = start + width(first);
            if (boundary) {
                int node = nodes.first().next.value(folded(first), -1);
                int end = after, bestEnd = start, bestNode = -1;
                while (node >= 0) {
                    const bool finished = end == text.size();
                    const auto next = finished ? U'\0' : scalarAt(text, end);
                    if (!nodes.at(node).groups.isEmpty() && (finished || !word(next))) {
                        bestEnd = end;
                        bestNode = node;
                    }
                    if (finished) break;
                    node = nodes.at(node).next.value(folded(next), -1);
                    end += width(next);
                }
                // At one start only the longest alias survives. Across starts
                // keep crossing spans but suppress any span wholly contained
                // in an earlier mention (including names from the other list).
                if (bestNode >= 0 && bestEnd > coveredUntil) {
                    hits.append({start, bestEnd, nodes.at(bestNode).groups});
                    coveredUntil = bestEnd;
                }
            }
            boundary = !word(first);
            start = after;
        }
        return hits;
    }

    QList<SynonymSuggestion> descendants(int node, int start, int prefixLength,
                                         bool characters, bool recurring, int limit) const
    {
        QList<SynonymSuggestion> result;
        QList<int> pending{node};
        while (!pending.isEmpty()) {
            const auto &entry = nodes.at(pending.takeLast());
            if (!(characters && entry.hasCharacters) && !(recurring && entry.hasRecurring)) continue;
            const auto &alias = characters && !entry.characterAlias.isEmpty() ? entry.characterAlias
                              : recurring ? entry.recurringAlias : QString();
            if (!alias.isEmpty()) {
                int offset = 0;
                for (int i = 0; i < prefixLength; ++i)
                    offset += width(scalarAt(alias, offset));
                if (offset < alias.size()) {
                    result.append({alias, alias.mid(offset), start});
                    if (limit > 0 && result.size() >= limit) break;
                }
            }
            for (const int child : entry.next) pending.append(child);
        }
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
            return a.alias.compare(b.alias, Qt::CaseInsensitive) < 0;
        });
        return result;
    }
};

SynonymMatcher::SynonymMatcher(QTextDocument *document, QObject *parent)
    : QObject(parent), d(std::make_unique<Data>())
{
    d->document = document;
    connect(document, &QTextDocument::contentsChange, this, [this, document](int pos, int removed, int added) {
        if (!removed && !added) return;
        // Qt can reuse a block revision while clearing and then filling a
        // document. Invalidate the actual changed interval before views query it.
        const int end = document->characterCount() - 1;
        auto block = document->findBlock(std::clamp(pos, 0, end));
        const int last = int(std::clamp<qint64>(qint64(pos) + added, 0, end));
        for (; block.isValid() && block.position() <= last; block = block.next()) {
            auto *cache = dynamic_cast<BlockMentions *>(block.userData());
            if (cache && cache->owner == this) cache->owner.clear();
        }
    });
}

SynonymMatcher::~SynonymMatcher() = default;

void SynonymMatcher::setGroups(const QList<SynonymEntry> &characters, const QList<SynonymEntry> &recurring,
                               const QStringList &genres)
{
    d->nodes = {PrefixNode{}};
    d->groups.clear();
    d->longest = d->longestCompletable = 0;
    ++d->generation;
    for (const auto &entry : characters)
        d->addGroup(entry.aliases, NameSource::Characters, !entry.excludeFromAutocomplete);
    for (const auto &entry : recurring)
        d->addGroup(entry.aliases, NameSource::Recurring, !entry.excludeFromAutocomplete);
    for (const auto &genre : genres) d->addGroup({genre}, NameSource::Genre, false);
}

const QList<SynonymGroup> &SynonymMatcher::groups() const { return d->groups; }

QList<SynonymMention> SynonymMatcher::mentions(const QTextBlock &block) const
{
    if (!block.isValid() || block.document() != d->document || !d->longest) return {};
    // The block owns its cache. Splitting/deleting a paragraph needs no global
    // offset updates. Word wrapping changes neither the block nor its revision.
    auto *cache = dynamic_cast<BlockMentions *>(block.userData());
    QList<SynonymMention> result;
    if (cache && cache->owner == this && cache->generation == d->generation && cache->revision == block.revision()) {
        result = cache->relative;
    } else {
        result = d->scan(ManuscriptText::blockText(block));
        if (!block.userData()) {
            cache = new BlockMentions;
            QTextBlock(block).setUserData(cache);
        }
        if (cache) {
            cache->owner = this;
            cache->generation = d->generation;
            cache->revision = block.revision();
            cache->relative = result;
        }
    }
    const int absolute = block.position();
    for (auto &hit : result) { hit.start += absolute; hit.end += absolute; }
    return result;
}

std::optional<SynonymMention> SynonymMatcher::mentionAt(int position) const
{
    if (!d->document) return {};
    std::optional<SynonymMention> best;
    bool inside = false;
    for (const auto &hit : mentions(d->document->findBlock(position))) {
        if (hit.start > position) break;
        if (hit.start == position) return hit;
        if (position > hit.end) continue;
        const bool contains = position < hit.end;
        if (!best || (contains && !inside) ||
            (contains == inside && hit.end - hit.start >= best->end - best->start)) {
            best = hit;
            inside = contains;
        }
    }
    return best;
}

std::optional<SynonymMention> SynonymMatcher::neighbor(const SynonymMention &origin, int group, bool backward) const
{
    if (!d->document || group < 0 || group >= d->groups.size() || !origin.groups.contains(group)) return {};
    auto block = d->document->findBlock(origin.start);
    for (; block.isValid(); block = backward ? block.previous() : block.next()) {
        const auto hits = mentions(block);
        if (backward) {
            for (auto it = hits.crbegin(); it != hits.crend(); ++it)
                if (it->start < origin.start && it->groups.contains(group)) return *it;
        } else {
            for (const auto &hit : hits)
                if (hit.start > origin.start && hit.groups.contains(group)) return hit;
        }
    }
    return {};
}

QList<SynonymSuggestion> SynonymMatcher::completions(int position, bool characters, bool recurring,
                                                     int minimumPrefix, int limit) const
{
    if (!d->document || !d->longestCompletable || (!characters && !recurring)) return {};
    const QTextBlock block = d->document->findBlock(position);
    if (!block.isValid() || position == block.position()) return {};
    // Bound the slice by the longest alias, even for a chapter-length paragraph.
    const int first = int(std::max<qint64>(block.position(), qint64(position) - 2LL * d->longestCompletable - 2));
    const int last = int(std::min<qint64>(block.position() + block.length() - 1, qint64(position) + 2));
    QTextCursor c(block);
    c.setPosition(first);
    c.setPosition(last, QTextCursor::KeepAnchor);
    const auto text = ManuscriptText::selectedText(c);
    const int caret = position - first;
    if (caret < text.size() && word(scalarAt(text, caret))) return {};
    bool boundary = first == block.position();
    for (int start = 0; start < caret;) {
        const auto initial = scalarAt(text, start);
        if (boundary) {
            int node = 0, length = 0;
            for (int i = start; i < caret && node >= 0; ++length) {
                const auto scalar = scalarAt(text, i);
                node = d->nodes.at(node).next.value(folded(scalar), -1);
                i += width(scalar);
            }
            if (node >= 0 && length >= minimumPrefix) {
                const auto &candidate = d->nodes.at(node);
                if ((characters && candidate.hasCharacters) || (recurring && candidate.hasRecurring))
                    return d->descendants(node, first + start, length, characters, recurring, limit);
            }
        }
        boundary = !word(initial);
        start += width(initial);
    }
    return {};
}
