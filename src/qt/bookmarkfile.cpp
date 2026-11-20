#include "bookmarkfile.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLockFile>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <algorithm>
#include <limits>
#include <utility>

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString absolute(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool readDisk(const QString &path, std::optional<QByteArray> &snapshot, QString *error)
{
    snapshot.reset();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QFileInfo info(path);
        if (!info.exists() && !info.isSymLink())
            return true;
        return fail(error, QStringLiteral("Cannot open %1. %2").arg(path, file.errorString()));
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("Cannot read %1. %2").arg(path, file.errorString()));
    snapshot = bytes;
    return true;
}

bool integer(const QJsonValue &value, int minimum, int &result)
{
    if (!value.isDouble())
        return false;
    const qint64 number = value.toInteger(-1);
    if (number < minimum || number > std::numeric_limits<int>::max())
        return false;
    result = static_cast<int>(number);
    return true;
}

int findBookmark(const QList<LineBookmark> &entries, const QString &category, const QString &name)
{
    for (qsizetype i = 0; i < entries.size(); ++i)
        if (entries.at(i).category == category && entries.at(i).name == name)
            return static_cast<int>(i);
    return -1;
}

bool sameCategory(const QList<LineBookmark> &entries, const QString &category)
{
    return std::any_of(entries.cbegin(), entries.cend(), [&](const LineBookmark &entry) {
        return entry.category == category;
    });
}

} // namespace

QString BookmarkFile::sidecarPath(const QString &manuscriptPath)
{
    return manuscriptPath.isEmpty() ? QString() : manuscriptPath + QStringLiteral(".bookmarks");
}

QStringList BookmarkFile::categories() const
{
    QSet<QString> unique;
    for (const LineBookmark &entry : data_.entries)
        unique.insert(entry.category);
    QStringList result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return result;
}

QString BookmarkFile::nextName(const QString &category) const
{
    const int current = data_.counters.value(category.trimmed());
    if (current == std::numeric_limits<int>::max())
        return {};
    return QString::number(current + 1);
}

void BookmarkFile::noteName(Data &data, const QString &category, const QString &name)
{
    bool ok;
    const int n = name.toInt(&ok);
    if (ok && n > 0 && QString::number(n) == name && n > data.counters.value(category))
        data.counters[category] = n;
}

QByteArray BookmarkFile::encode(const Data &data)
{
    QJsonArray entries;
    for (const LineBookmark &entry : data.entries) {
        entries.append(QJsonObject{{"category", entry.category}, {"name", entry.name},
                                   {"line", entry.line}});
    }
    QJsonObject counters;
    for (auto it = data.counters.constBegin(); it != data.counters.constEnd(); ++it)
        counters.insert(it.key(), it.value());
    return QJsonDocument(QJsonObject{{"version", 1}, {"lastCategory", data.lastCategory},
                                     {"counters", counters}, {"bookmarks", entries}})
        .toJson(QJsonDocument::Indented);
}

bool BookmarkFile::decode(const QByteArray &bytes, Data &data, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("Invalid bookmark JSON at byte %1. %2")
                    .arg(parseError.offset).arg(parseError.errorString()));
    const QJsonObject root = doc.object();
    if (!doc.isObject() || root.value("version").toInteger(-1) != 1 ||
        !root.value("bookmarks").isArray())
        return fail(error, QStringLiteral("Expected version 1 and a bookmarks array."));

    Data parsed;
    if (root.contains("lastCategory")) {
        parsed.lastCategory = root.value("lastCategory").toString().trimmed();
        if (parsed.lastCategory.isEmpty())
            return fail(error, QStringLiteral("The last category must have a name."));
    }
    if (root.contains("counters") && !root.value("counters").isObject())
        return fail(error, QStringLiteral("The category counters must be an object."));
    const QJsonObject counters = root.value("counters").toObject();
    for (auto it = counters.constBegin(); it != counters.constEnd(); ++it) {
        int count;
        if (it.key().trimmed().isEmpty() || !integer(it.value(), 0, count))
            return fail(error, QStringLiteral("Invalid bookmark counter for %1.").arg(it.key()));
        parsed.counters.insert(it.key().trimmed(), count);
    }

    QSet<QPair<QString, QString>> used;
    const QJsonArray entries = root.value("bookmarks").toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        LineBookmark entry{object.value("category").toString().trimmed(),
                           object.value("name").toString().trimmed(), 0};
        if (entry.category.isEmpty() || entry.name.isEmpty() ||
            !integer(object.value("line"), 1, entry.line))
            return fail(error, QStringLiteral("Each bookmark needs a category, name and positive line number."));
        const auto key = qMakePair(entry.category, entry.name);
        if (used.contains(key))
            return fail(error, QStringLiteral("Repeated bookmark %1 / %2.").arg(entry.category, entry.name));
        used.insert(key);
        noteName(parsed, entry.category, entry.name);
        parsed.entries.append(entry);
    }
    data = std::move(parsed);
    return true;
}

bool BookmarkFile::openManuscript(const QString &path, QString *error)
{
    manuscript_ = absolute(path);
    data_ = Data{};
    pending_ = false;
    readBlocked_ = false;
    baseline_.reset();
    unreadableJson_.reset();
    if (manuscript_.isEmpty())
        return true;
    const QString file = sidecarPath(manuscript_);
    if (!readDisk(file, baseline_, error)) {
        readBlocked_ = true;
        return false;
    }
    if (!baseline_)
        return true;
    QString reason;
    if (!decode(*baseline_, data_, &reason)) {
        readBlocked_ = true;
        unreadableJson_ = baseline_;
        return fail(error, QStringLiteral("Cannot load %1. %2").arg(file, reason));
    }
    return true;
}

bool BookmarkFile::writeBytes(const QByteArray &encoded, QString *error)
{
    const QString path = sidecarPath(manuscript_);
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return fail(error, QStringLiteral("Cannot lock %1 for writing. Retry after it becomes writable.").arg(path));

    std::optional<QByteArray> disk;
    if (!readDisk(path, disk, error))
        return false;
    if (disk && *disk == encoded) {
        baseline_ = disk;
        return true;
    }
    if (disk != baseline_)
        return fail(error, QStringLiteral("The bookmark file %1 changed on disk. Reload its bookmarks, "
                                          "or choose another manuscript name with Save As.").arg(path));

    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(encoded) != encoded.size())
        return fail(error, QStringLiteral("Cannot write %1. %2").arg(path, output.errorString()));

    std::optional<QByteArray> beforeCommit;
    if (!readDisk(path, beforeCommit, error))
        return false;
    if (beforeCommit != disk)
        return fail(error, QStringLiteral("The bookmark file changed while saving %1. Reload and retry.").arg(path));
    if (!output.commit())
        return fail(error, QStringLiteral("Cannot finish writing %1. %2").arg(path, output.errorString()));
    baseline_ = encoded;
    return true;
}

bool BookmarkFile::store(Data next, QString *error)
{
    if (readBlocked_)
        return fail(error, QStringLiteral("Reload the bookmark file after correcting its read error."));
    if (!manuscript_.isEmpty() && !writeBytes(encode(next), error))
        return false;
    data_ = std::move(next);
    pending_ = manuscript_.isEmpty();
    return true;
}

bool BookmarkFile::flush(QString *error)
{
    if (!pending_)
        return true;
    if (manuscript_.isEmpty())
        return fail(error, QStringLiteral("Save the manuscript to give its bookmarks a filename."));
    if (!writeBytes(unreadableJson_ ? *unreadableJson_ : encode(data_), error))
        return false;
    pending_ = false;
    return true;
}

bool BookmarkFile::saveAs(const QString &newPath, QString *error)
{
    const QString target = absolute(newPath);
    if (target.isEmpty())
        return fail(error, QStringLiteral("The manuscript needs a filename before its bookmarks can be saved."));
    if (target == manuscript_)
        return flush(error);

    // With no source bookmarks, an existing target sidecar belongs to the
    // chosen manuscript. A populated source is always copied from memory.
    if (readBlocked_ && !unreadableJson_) {
        const QString source = sidecarPath(manuscript_);
        QString destinationError;
        if (!openManuscript(target, &destinationError))
            return fail(error, destinationError);
        return fail(error, QStringLiteral("Source bookmarks at %1 could not be read for copying.").arg(source));
    }
    if (!pending_ && data_.entries.isEmpty() && data_.counters.isEmpty() && !baseline_)
        return openManuscript(target, error);
    manuscript_ = target;
    baseline_.reset();
    pending_ = true;
    return flush(error);
}

bool BookmarkFile::add(QString category, QString name, int line, QString *error)
{
    category = category.trimmed();
    name = name.trimmed();
    if (category.isEmpty() || line < 1)
        return fail(error, QStringLiteral("Choose a category and an existing manuscript line."));
    if (name.isEmpty())
        name = nextName(category);
    if (name.isEmpty())
        return fail(error, QStringLiteral("Enter a bookmark name. The category counter is full."));
    if (findBookmark(data_.entries, category, name) >= 0)
        return fail(error, QStringLiteral("The category already contains a bookmark named %1.").arg(name));

    Data next = data_;
    next.entries.append({category, name, line});
    noteName(next, category, name);
    next.lastCategory = category;
    return store(std::move(next), error);
}

bool BookmarkFile::renameBookmark(const QString &category, const QString &name,
                                  QString newCategory, QString newName, QString *error)
{
    newCategory = newCategory.trimmed();
    newName = newName.trimmed();
    const int original = findBookmark(data_.entries, category, name);
    if (original < 0)
        return fail(error, QStringLiteral("This bookmark no longer exists."));
    if (newCategory.isEmpty() || newName.isEmpty())
        return fail(error, QStringLiteral("Enter both a category and a name."));
    const int duplicate = findBookmark(data_.entries, newCategory, newName);
    if (duplicate >= 0 && duplicate != original)
        return fail(error, QStringLiteral("This category already contains that bookmark name."));
    if (category == newCategory && name == newName)
        return true;

    Data next = data_;
    next.entries[original].category = newCategory;
    next.entries[original].name = newName;
    noteName(next, newCategory, newName);
    next.lastCategory = newCategory;
    return store(std::move(next), error);
}

bool BookmarkFile::renameCategory(const QString &category, QString newName, QString *error)
{
    newName = newName.trimmed();
    if (newName.isEmpty())
        return fail(error, QStringLiteral("Enter a category name."));
    if (!sameCategory(data_.entries, category))
        return fail(error, QStringLiteral("This category no longer exists."));
    if (newName == category)
        return true;
    if (sameCategory(data_.entries, newName))
        return fail(error, QStringLiteral("A category with that name already exists."));

    Data next = data_;
    for (LineBookmark &entry : next.entries)
        if (entry.category == category)
            entry.category = newName;
    const int oldCounter = next.counters.take(category);
    next.counters[newName] = std::max(oldCounter, next.counters.value(newName));
    if (next.lastCategory == category)
        next.lastCategory = newName;
    return store(std::move(next), error);
}

bool BookmarkFile::remove(const QString &category, const QString &name, QString *error)
{
    Data next = data_;
    const auto removed = next.entries.removeIf([&](const LineBookmark &entry) {
        return entry.category == category && (name.isEmpty() || entry.name == name);
    });
    if (removed == 0)
        return fail(error, QStringLiteral("This bookmark or category no longer exists."));
    return store(std::move(next), error);
}
