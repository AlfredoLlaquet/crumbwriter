#pragma once

#include <QString>
#include <optional>

class QTextCursor;

// Resolves a complete selection command to an insertion point. The original
// cursor stays untouched on success and on a missing destination.
struct ViMotion {
    QString name;
    QString target;
    int count = 1;
    bool explicitCount = false;
    bool skipLastCharacter = false;

    static std::optional<ViMotion> parse(const QString &command, QString &hint);
    std::optional<int> destination(QTextCursor cursor) const;
};
