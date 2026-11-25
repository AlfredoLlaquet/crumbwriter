#include "bookmarkanchors.h"
#include "genericparagraph.h"
#include "manuscripttext.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QHash>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

// The final empty Qt block represents the previous line's LF, exactly as the
// editor's writingEnd() does. Genuine blank paragraphs before it remain targets.
int physicalLines(const QTextDocument *document)
{
    const auto last = document->lastBlock();
    return qMax(1, document->blockCount() - int(last.length() == 1));
}

// Opening an unchanged manuscript reads only the neighborhoods of bookmarks.
// If outside edits displaced one, the first lookup builds one shared line index.
class LineIndex {
public:
    explicit LineIndex(const QTextDocument *doc) : document(doc), count(physicalLines(doc)) {}

    QByteArray fingerprint(int line)
    {
        if (line < 1 || line > count) return {};
        auto it = hashes.constFind(line);
        if (it != hashes.cend()) return *it;
        const QByteArray hash = digest(document->findBlockByNumber(line - 1));
        hashes.insert(line, hash);
        return hash;
    }

    BookmarkContext context(int line)
    {
        return {fingerprint(line - 1), fingerprint(line), fingerprint(line + 1)};
    }

    int locate(const LineBookmark &entry)
    {
        const int saved = qBound(1, entry.line, count);
        const BookmarkContext &old = entry.context;
        if (old.target.isEmpty() || context(saved) == old) return saved;
        if (!indexed) {
            auto block = document->begin();
            for (int line = 1; line <= count; ++line, block = block.next()) {
                auto it = hashes.constFind(line);
                const QByteArray hash = it == hashes.cend() ? digest(block) : *it;
                hashes.insert(line, hash);
                occurrences[hash].append(line);
            }
            indexed = true;
        }

        // Identical paragraphs are common. Prefer the matching neighborhood,
        // then the nearest old position; document order breaks an exact tie.
        int best = 0;
        int bestScore = -1;
        auto consider = [&](int line, int score) {
            line = qBound(1, line, count);
            if (score > bestScore || (score == bestScore &&
                (std::abs(line - saved) < std::abs(best - saved) ||
                (std::abs(line - saved) == std::abs(best - saved) && line < best)))) {
                best = line;
                bestScore = score;
            }
        };
        for (int line : occurrences.value(old.target))
            consider(line, int(fingerprint(line - 1) == old.before) +
                           int(fingerprint(line + 1) == old.after));
        if (best) return best;

        // The target was changed or deleted outside the editor. Adjacent old
        // neighbors now touch after a deletion or bracket a rewritten line.
        const auto following = old.after.isEmpty() ? QList<int>{count + 1} : occurrences.value(old.after);
        for (int after : following) {
            if (after >= 2 && fingerprint(after - 2) == old.before)
                consider(after - 1, 2);
            if (fingerprint(after - 1) == old.before)
                consider(after, 2);
            // EOF alone cannot identify a missing passage in a different
            // manuscript. Only a real matching neighbor counts on its own.
            if (!old.after.isEmpty()) consider(after, 1);
        }
        if (best) return best;
        for (int before : occurrences.value(old.before))
            consider(before + 1, 1);
        return best ? best : saved;
    }

private:
    static QByteArray digest(const QTextBlock &block)
    {
        const QString text = ManuscriptText::blockText(block);
        // Publishing can resize a heading's underline. Its position among
        // its neighbors and its level identify it regardless of the saved length.
        const QByteArray bytes = GenericParagraph::markerLevel(text)
            ? QByteArray(1, text.front().toLatin1()) : text.toUtf8();
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    }
    const QTextDocument *document;
    int count;
    QHash<int, QByteArray> hashes;
    QHash<QByteArray, QList<int>> occurrences;
    bool indexed = false;
};

} // namespace

BookmarkAnchors::BookmarkAnchors(QTextDocument *document) : document_(document)
{
    if (document_) {
        document_->documentLayout();
        physicalLineCount_ = lineCount();
        undoDepth_ = document_->availableUndoSteps();
    }
}

int BookmarkAnchors::lineCount() const
{
    return document_ ? physicalLines(document_) : std::numeric_limits<int>::max();
}

void BookmarkAnchors::attach(LineBookmark &entry, int line) const
{
    entry.line = qBound(1, line, lineCount());
    if (!document_) return;
    entry.anchor = QTextCursor(document_->findBlockByNumber(entry.line - 1));
    entry.undoLines = {{document_->availableUndoSteps(), entry.line}};
}

bool BookmarkAnchors::restore(QList<LineBookmark> &entries)
{
    if (!document_) return false;
    newCommand_ = false;
    physicalLineCount_ = lineCount();
    undoDepth_ = document_->availableUndoSteps();
    LineIndex index(document_);
    bool changed = false;
    for (LineBookmark &entry : entries) {
        const int line = index.locate(entry);
        const BookmarkContext context = index.context(line);
        changed |= line != entry.line || !(context == entry.context);
        attach(entry, line);
        entry.context = context;
    }
    return changed;
}

void BookmarkAnchors::capture(QList<LineBookmark> &entries) const
{
    if (!document_ || entries.isEmpty()) return;
    LineIndex index(document_);
    for (LineBookmark &entry : entries)
        entry.context = index.context(entry.line);
}

bool BookmarkAnchors::edited(QList<LineBookmark> &entries, int position, int added,
                             bool alreadyPending, LineBookmark *openForm)
{
    if (!document_) return false;
    const int depth = document_->availableUndoSteps();
    const int previousDepth = undoDepth_;
    const bool fresh = newCommand_;
    const int count = lineCount();
    const bool changedCount = count != physicalLineCount_;
    physicalLineCount_ = count;
    undoDepth_ = depth;
    newCommand_ = false;
    if (entries.isEmpty() && !openForm) return false;
    const int end = document_->characterCount() - 1;
    const int firstLine = document_->findBlock(qBound(0, position, end)).blockNumber() + 1;
    const int rangeEnd = int(std::clamp<qint64>(qint64(position) + added, 0, end));
    const int lastLine = document_->findBlock(rangeEnd).blockNumber() + 1;
    const bool replay = !fresh && depth != previousDepth;
    LineIndex index(document_);
    bool changed = false;
    auto track = [&](LineBookmark &entry) {
        if (entry.anchor.isNull()) return;
        auto &history = entry.undoLines;
        if (fresh && !history.isEmpty() && history.lastKey() > previousDepth)
            history.erase(history.upperBound(previousDepth), history.end());
        // An ordinary keystroke cannot move distant line starts. Qt already
        // adjusts their character offsets; avoid a tree lookup per bookmark.
        if (!changedCount && firstLine == lastLine && !replay &&
            (entry.line < firstLine - 1 || entry.line > firstLine + 1)) return;
        int line = qBound(1, entry.anchor.blockNumber() + 1, count);
        if (replay) {
            // A Qt cursor survives insertions and deletions, but reinserting a
            // deleted target with Undo would otherwise stick to its successor.
            auto saved = history.upperBound(depth);
            if (saved != history.begin()) {
                line = qBound(1, *--saved, count);
            } else {
                // This bookmark was created after the edit now being undone.
                history.insert(depth, line);
            }
        } else if (line != entry.line) {
            history.insert(depth, line);
        }
        const bool moved = entry.line != line;
        entry.line = line;
        const auto block = document_->findBlockByNumber(line - 1);
        if (entry.anchor.position() != block.position())
            entry.anchor.setPosition(block.position());
        if (&entry == openForm) return;
        changed |= moved;
        if (!changed && !alreadyPending && line + 1 >= firstLine && line - 1 <= lastLine)
            changed = !(entry.context == index.context(line));
    };
    for (LineBookmark &entry : entries) track(entry);
    if (openForm) track(*openForm);
    return changed;
}
