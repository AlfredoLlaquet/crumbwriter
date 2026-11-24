#pragma once

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <vector>

struct CrumbRecord;

// A day starts with the recognized records already in the manuscript. Keep
// their identities so deleting an older crumb cannot reduce today's output.
// Zero is a physical row that does not currently have a recognized header.
class DailyCount {
public:
    qint64 count() const;
    QDate date() const { return date_; }
    qint64 wordChange() const;
    QString setCount(bool words, qint64 value);
    void setTotalWords(qint64 words);

    QString open(const QString &manuscript, QDate date, const QVector<quint64> &numbers,
                 qint64 words, int trimmedTrailingRows);
    QString saveAs(const QString &manuscript);
    QByteArray snapshot();
    QString publish(const QString &manuscript, const QByteArray &snapshot);
    QString startDay(QDate date);
    QString replace(int first, int oldEnd, const std::vector<CrumbRecord> &before,
                    const std::vector<CrumbRecord> &parsed, int editStart, int removed, int added);

private:
    struct Row {
        quint64 number = 0;
        int origin = -1; // Index of an older crumb, or -1 for a new/empty row.
    };
    // An adjustment belongs to the calendar day, independently of the
    // manuscript revisions that a background save may still be publishing.
    struct Adjustment {
        qint64 value = 0;
        qint64 measured = 0;
        qint64 apply(qint64 now) const;
    };

    void takeBaseline(QDate date);
    void indexOrigins();
    bool matchOpened(int trimmedTrailingRows);
    int availableOrigin(quint64 number) const;
    bool addAlias(int origin, quint64 number);
    qsizetype headerExtent() const;
    QByteArray shape(qsizetype count) const;
    QString writeState() const;
    QString writeState(const QString &file, const QByteArray &checkpoint) const;
    bool readState(const QByteArray &bytes, QDate date);

    QVector<Row> rows_;
    QVector<quint64> baseline_;
    QHash<int, QVector<quint64>> aliases_;
    QHash<int, quint64> preferred_;
    QHash<quint64, QVector<int>> originsByNumber_;
    QVector<bool> occupied_;
    QString stateFile_;
    QByteArray cached_, committed_, lastDraftCheckpoint_;
    QJsonObject cachedLayout_;
    QString lastDraftPath_;
    QDate date_;
    int currentCount_ = 0;
    qint64 totalWords_ = 0, baselineWords_ = 0;
    Adjustment crumbSetting_, wordSetting_;
};
