#ifndef CRUMBWRITER_COMMITTEDPUNCTUATION_H
#define CRUMBWRITER_COMMITTEDPUNCTUATION_H

#include <QtCore/QString>

class QTextDocument;

// Only characters observed through a key or IM commit are eligible. Stored
// offsets cover the short unfinished sequence directly before the next input.
class CommittedPunctuation {
public:
    void feed(QTextDocument *doc, int start, const QString &inserted);
    void finish(QTextDocument *doc);
    void reset();
    bool pending() const { return length_ != 0 || elisionAt_ >= 0; }
private:
    int resolve(QTextDocument *doc);
    void resolveElision(QTextDocument *doc, bool boundary);
    int start_ = 0;
    QChar character_;
    int length_ = 0;
    int elisionAt_ = -1;
    int elisionEnd_ = -1;
};
#endif
