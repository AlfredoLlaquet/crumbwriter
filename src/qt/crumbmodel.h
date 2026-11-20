#ifndef CRUMBWRITER_CRUMBMODEL_H
#define CRUMBWRITER_CRUMBMODEL_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <functional>
#include <memory>
#include <optional>

class QTextDocument;

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
};

class CrumbModel final : public QObject {
public:
    explicit CrumbModel(QTextDocument *document);
    ~CrumbModel() override;

    // A record reference remains valid until the next document edit.
    const CrumbRecord &record(int line) const;
    QVector<CrumbGenre> genres() const;
    int sessionCount() const;
    void markOpened();

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
