#pragma once

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QString>
#include <QVector>
#include <vector>

struct CrumbRecord;

// A day starts with the recognized records already in the manuscript. Keep
// their identities so deleting an older crumb cannot reduce today's output.
// Zero is a physical row that does not currently have a recognized header.
class DailyCount {
public:
    int count() const { return currentCount_; }
    QDate date() const { return date_; }

    QString open(const QString &manuscript, QDate date, const QVector<quint64> &numbers);
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

    void takeBaseline(QDate date);
    void indexOrigins();
    bool matchOpened();
    int availableOrigin(quint64 number) const;
    bool addAlias(int origin, quint64 number);
    QByteArray shape() const;
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
    QString lastDraftPath_;
    QDate date_;
    int currentCount_ = 0;
};
