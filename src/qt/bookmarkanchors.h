#ifndef CRUMBWRITER_BOOKMARKANCHORS_H
#define CRUMBWRITER_BOOKMARKANCHORS_H

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtGui/QTextCursor>

class QTextDocument;

struct BookmarkContext {
    QByteArray before;
    QByteArray target;
    QByteArray after;

    bool operator==(const BookmarkContext &other) const
    { return before == other.before && target == other.target && after == other.after; }
};

struct LineBookmark {
    QString category;
    QString name;
    int line = 0; // One-based physical file line.
    BookmarkContext context;
    QTextCursor anchor;
    QMap<int, int> undoLines; // Changes of destination, keyed by Qt undo-stack depth.
};

class BookmarkAnchors {
public:
    explicit BookmarkAnchors(QTextDocument *document = nullptr);
    int lineCount() const;
    void attach(LineBookmark &entry, int line) const;
    bool restore(QList<LineBookmark> &entries);
    void capture(QList<LineBookmark> &entries) const;
    void noteUndoCommand() { newCommand_ = true; }
    bool edited(QList<LineBookmark> &entries, int position, int added, bool alreadyPending,
                LineBookmark *openForm = nullptr);

private:
    QTextDocument *document_;
    int physicalLineCount_ = 1;
    int undoDepth_ = 0;
    bool newCommand_ = false;
};

#endif
