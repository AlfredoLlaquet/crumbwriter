#ifndef CRUMBWRITER_CRUMBMODEL_H
#define CRUMBWRITER_CRUMBMODEL_H

#include <QtCore/QDate>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <functional>
#include <memory>
#include <optional>

class QTextDocument;

enum class ParagraphRole { Body, Blank, DocumentTitle, ChapterTitle };

// Line numbers are zero-based. All columns are relative UTF-16 offsets, just
// like QTextBlock and QTextCursor. Each end is exclusive.
struct CrumbWarning {
    int line = 0;
    int start = 0;
    int end = 0;
    QString actual;
    QString expected;
    QString genre; // Empty for a header warning.
};

struct CrumbGenrePart {
    QString key;
    QString name;
    quint64 number = 0;
    int start = 0; // Number digits within the physical line.
    int end = 0;
    bool overflow = false;
    bool warning = false;
};

struct CrumbRecord {
    bool valid = false;
    quint64 number = 0;
    QString text; // Exact block text, used to ignore format-only notifications.
    QString label;
    QString title;
    int words = 0;
    int fileWords = 0; // Whole-file convention, including any first-line work title.
    bool blank = true;
    bool hasWorkSeparator = false; // Meaningful on physical line 1 only.
    ParagraphRole role = ParagraphRole::Body;
    int recordStart = 0;
    int headerStart = 0;
    int digitsStart = 0;
    int headerEnd = 0;
    int titleStart = 0;
    int titleEnd = 0;
    int bodyStart = 0;
    bool headerWarning = false;
    QVector<CrumbGenrePart> genres;
    QVector<CrumbWarning> warnings;
};

struct CrumbGenre {
    QString key;
    QString name;
    quint64 number = 0;
    bool warning = false;
    bool canAppend = true;
};

struct CrumbChange {
    int firstLine = 0;
    int lastLine = 0;
    QVector<int> otherLines; // Changed warning ranges outside the edited text.
    bool genresChanged = false;
    bool structureChanged = false;
    bool textChanged = false;
    bool modeChanged = false;
    bool titlesChanged = false;
};

class CrumbModel final : public QObject {
public:
    explicit CrumbModel(QTextDocument *document, std::function<QDate()> localDate = QDate::currentDate);
    ~CrumbModel() override;

    // A record reference remains valid until the next document edit.
    const CrumbRecord &record(int line) const;
    bool isGeneric() const;
    ParagraphRole paragraphRole(int line) const;
    QVector<int> titleLines() const; // Zero-based, in document order.
    int neighborTitle(int line, bool backward) const;
    int paragraphCount() const; // Nonblank physical paragraphs, including headings.
    QVector<CrumbGenre> genres() const;
    int crumbCount() const;
    qint64 totalWords() const;
    int todayCount() const;
    qint64 todayWords() const;
    void markOpened();
    QString openDailyCount(const QString &manuscript);
    QString saveDailyCountAs(const QString &manuscript);
    QByteArray dailySnapshot();
    QString publishDailySnapshot(const QString &manuscript, const QByteArray &snapshot);
    void refreshDay();
    std::function<void(const QString &)> onDailyCountError;

    int findNumber(quint64 number, int absoluteOrigin, bool backward, bool wrap,
                   bool includeOrigin = true) const;
    int neighbor(int line, bool backward) const;
    QString nextHeader(const QString &title = {}) const;
    QString nextGenreTitle(const QString &name) const;
    QString completeGenre(int line, int hashColumn) const;
    std::optional<CrumbWarning> nextWarning(int absoluteOrigin, bool backward,
                                            bool wrap = true) const;

    void observe(QObject *owner, std::function<void(const CrumbChange &)> handler);

private:
    struct Data;
    std::unique_ptr<Data> d;
    void update(int position, int removed, int added);
    void notify(const CrumbChange &change);
};

#endif
