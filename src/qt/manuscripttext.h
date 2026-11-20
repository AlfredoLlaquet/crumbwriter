#ifndef CRUMBWRITER_MANUSCRIPTTEXT_H
#define CRUMBWRITER_MANUSCRIPTTEXT_H

#include <QtCore/QString>
class QTextBlock;
class QTextCursor;
class QTextDocument;

namespace ManuscriptText {
// Qt reserves three valid UTF-16 code units for its own document structure.
// Keep their original value as character-format metadata on one visible code
// unit so manuscript offsets, undo, and clipboard positions stay identical.
void load(QTextDocument *doc, const QString &text);
void insert(QTextCursor &cursor, const QString &text);
QString documentText(const QTextDocument *doc);
QString blockText(const QTextBlock &block);
QString selectedText(const QTextCursor &cursor);
}
#endif
