#ifndef CRUMBWRITER_MANUSCRIPTTEXT_H
#define CRUMBWRITER_MANUSCRIPTTEXT_H

#include <QtCore/QString>
#include <QtCore/QVector>
#include <QTextCursor>
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
bool crossHiddenMarker(QTextCursor &cursor, bool backward, QTextCursor::MoveMode mode);
QTextBlock adjacentVisibleBlock(QTextBlock block, bool backward);
void replaceVisibleRange(QTextCursor &cursor, const QString &text);

// A search runs against the visible writing. Its hits still have to address
// the original QTextDocument used by selections, bookmarks and undo.
class VisibleText {
public:
    QString reset(const QTextDocument *document, bool foldMarkers);
    int sourcePosition(int visible) const;
    int visiblePosition(int source) const;
private:
    struct Fold { int source, visible, length; };
    QVector<Fold> folds_;
};
}
#endif
