#ifndef CRUMBWRITER_BOOKMARKFILE_H
#define CRUMBWRITER_BOOKMARKFILE_H

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QStringList>
#include <optional>

struct LineBookmark {
    QString category;
    QString name;
    int line = 0; // One-based physical line, independent of edits to the manuscript.
};

class BookmarkFile {
public:
    bool openManuscript(const QString &path, QString *error = nullptr);
    bool saveAs(const QString &newPath, QString *error = nullptr);
    bool flush(QString *error = nullptr);
    QString manuscriptPath() const { return manuscript_; }
    bool hasPending() const { return pending_; }

    const QList<LineBookmark> &entries() const { return data_.entries; }
    QString lastCategory() const { return data_.lastCategory; }
    QStringList categories() const;
    QString nextName(const QString &category) const;

    bool add(QString category, QString name, int line, QString *error = nullptr);
    bool renameBookmark(const QString &category, const QString &name,
                        QString newCategory, QString newName, QString *error = nullptr);
    bool renameCategory(const QString &category, QString newName, QString *error = nullptr);
    // An empty name designates the entire category.
    bool remove(const QString &category, const QString &name, QString *error = nullptr);

    static QString sidecarPath(const QString &manuscriptPath);

private:
    struct Data {
        QList<LineBookmark> entries;
        QMap<QString, int> counters; // Highest assigned counter, including deleted entries.
        QString lastCategory = QStringLiteral("General");
    };
    static bool decode(const QByteArray &bytes, Data &data, QString *error);
    static QByteArray encode(const Data &data);
    static void noteName(Data &data, const QString &category, const QString &name);
    bool store(Data next, QString *error);
    bool writeBytes(const QByteArray &encoded, QString *error);

    Data data_;
    QString manuscript_;
    std::optional<QByteArray> baseline_; // Missing sidecar and zero-byte sidecar differ.
    std::optional<QByteArray> unreadableJson_; // Save As can still copy malformed text exactly.
    bool pending_ = false;
    bool readBlocked_ = false;
};

#endif
