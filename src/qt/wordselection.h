#pragma once

#include <QTextCursor>

namespace WordSelection {
// Pointer gestures target a glyph. A keyboard caret can also sit just after
// a word, as it does at the end of a paragraph.
QTextCursor atCharacter(QTextCursor cursor);
QTextCursor atCaret(QTextCursor cursor);
}
