#pragma once

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

struct SpellingRange {
    int start;
    int end;
};

// Ranges are offsets into the original QString, including supplementary letters.
class SpellWords {
public:
    static QString key(QString word);
    static bool loading();
    static bool available();
    static QStringList suggestions(const QString &word);

    void setExceptions(const QStringList &aliases, const QStringList &dictionary);
    QVector<SpellingRange> issues(const QString &text, int start = 0) const;

private:
    struct Phrase {
        QString text;
        int firstWordOffset;
    };
    QVector<SpellingRange> phraseRanges(const QString &text, int start) const;
    QHash<QString, QVector<Phrase>> phrases_;
    QSet<QString> accepted_;
};
