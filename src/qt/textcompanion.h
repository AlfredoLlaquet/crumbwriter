#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// A manuscript sidecar has a missing state distinct from an empty file.
// The caller validates its text format and decides when to publish an edit.
class TextCompanion {
public:
    explicit TextCompanion(QString suffix);
    bool openManuscript(const QString &path, QString *error = nullptr);
    bool saveAs(const QString &newPath, QString *error = nullptr);
    bool write(const QByteArray &bytes, QString *error = nullptr);
    bool flush(QString *error = nullptr);
    bool hasPending() const { return pending_; }
    const QByteArray &bytes() const { return bytes_; }
    const QString &manuscriptPath() const { return manuscript_; }
    QString sidecarPath() const;

private:
    bool publish(const QByteArray &bytes, QString *error);
    QString suffix_;
    QString manuscript_;
    QByteArray bytes_;
    std::optional<QByteArray> baseline_;
    bool pending_ = false;
    bool unreadable_ = false;
};
