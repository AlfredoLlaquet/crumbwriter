#include "wordselection.h"
#include "manuscripttext.h"

#include <QRegularExpression>
#include <QTextBlock>

namespace {
QTextCursor tokenAt(QTextCursor cursor, bool caret)
{
    static const QRegularExpression tokens(QStringLiteral(
        R"(([\p{L}\p{M}\p{N}_]+(?:['’ʼ‐‑-][\p{L}\p{M}\p{N}_]+)*)|\h+|\X)"));
    cursor.clearSelection();
    const auto block = cursor.block();
    const int offset = cursor.positionInBlock();
    const QString text = ManuscriptText::blockText(block);
    int start = -1;
    int end = -1;
    auto matches = tokens.globalMatch(text);
    while (matches.hasNext()) {
        const auto token = matches.next();
        const int first = int(token.capturedStart());
        const int last = int(token.capturedEnd());
        if (last < offset) continue;
        const bool word = token.capturedStart(1) >= 0;
        if (last == offset) {
            if (caret && word) { start = first; end = last; }
            continue;
        }
        if (!caret || word || text[first] == u'—' || text[first] == u'–') {
            start = first;
            end = last;
        }
        break;
    }
    if (start >= 0) {
        cursor.setPosition(block.position() + start);
        cursor.setPosition(block.position() + end, QTextCursor::KeepAnchor);
    }
    return cursor;
}
}

QTextCursor WordSelection::atCharacter(QTextCursor cursor) { return tokenAt(cursor, false); }
QTextCursor WordSelection::atCaret(QTextCursor cursor) { return tokenAt(cursor, true); }
