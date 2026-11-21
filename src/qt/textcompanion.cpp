#include "textcompanion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <utility>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString absolutePath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool readBytes(const QString &path, std::optional<QByteArray> &result, QString *error)
{
    result.reset();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) return true;
        return fail(error, QStringLiteral("Cannot open %1. %2").arg(path, file.errorString()));
    }
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("Cannot read %1. %2").arg(path, file.errorString()));
    result = std::move(bytes);
    return true;
}
}

TextCompanion::TextCompanion(QString suffix) : suffix_(std::move(suffix)) {}

QString TextCompanion::sidecarPath() const
{
    return manuscript_.isEmpty() ? QString() : manuscript_ + suffix_;
}

bool TextCompanion::openManuscript(const QString &path, QString *error)
{
    manuscript_ = absolutePath(path);
    bytes_.clear();
    baseline_.reset();
    pending_ = false;
    unreadable_ = !manuscript_.isEmpty() && !readBytes(sidecarPath(), baseline_, error);
    if (baseline_) bytes_ = *baseline_;
    return !unreadable_;
}

bool TextCompanion::publish(const QByteArray &next, QString *error)
{
    const QString path = sidecarPath();
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return fail(error, QStringLiteral("Cannot lock %1. Check the directory and retry.").arg(path));

    std::optional<QByteArray> disk;
    if (!readBytes(path, disk, error)) return false;
    if (disk && *disk == next) {
        baseline_ = disk;
        return true;
    }
    if (disk != baseline_)
        return fail(error, QStringLiteral("%1 changed on disk. Reload the list or use Save As with another manuscript name.").arg(path));

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(next) != next.size())
        return fail(error, QStringLiteral("Cannot write %1. %2").arg(path, file.errorString()));
    std::optional<QByteArray> beforeCommit;
    if (!readBytes(path, beforeCommit, error)) return false;
    if (beforeCommit != disk)
        return fail(error, QStringLiteral("%1 changed while saving. Reload and retry.").arg(path));
    if (!file.commit())
        return fail(error, QStringLiteral("Cannot finish writing %1. %2").arg(path, file.errorString()));
    baseline_ = next;
    return true;
}

bool TextCompanion::write(const QByteArray &bytes, QString *error)
{
    if (unreadable_)
        return fail(error, QStringLiteral("Reload %1 after correcting its read error.").arg(sidecarPath()));
    if (!manuscript_.isEmpty() && !publish(bytes, error)) return false;
    bytes_ = bytes;
    pending_ = manuscript_.isEmpty();
    return true;
}

bool TextCompanion::flush(QString *error)
{
    if (!pending_) return true;
    if (manuscript_.isEmpty())
        return fail(error, QStringLiteral("Save the manuscript to give its %1 file a name.").arg(suffix_));
    return write(bytes_, error);
}

bool TextCompanion::saveAs(const QString &newPath, QString *error)
{
    const QString target = absolutePath(newPath);
    if (target.isEmpty())
        return fail(error, QStringLiteral("Choose a manuscript filename before saving the %1 file.").arg(suffix_));
    if (target == manuscript_) return flush(error);

    if (unreadable_) {
        const QString source = sidecarPath();
        // The manuscript has already moved. Its source companion could not be
        // read, so preserve either existing sidecar and report the failed copy.
        if (!openManuscript(target, error)) return false;
        return fail(error, QStringLiteral("Source file %1 could not be read for copying.").arg(source));
    }
    if (!pending_ && !baseline_)
        return openManuscript(target, error);

    manuscript_ = target;
    baseline_.reset();
    pending_ = true;
    return flush(error);
}
