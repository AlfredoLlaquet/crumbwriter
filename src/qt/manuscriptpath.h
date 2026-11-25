#pragma once

#include <QFileInfo>
#include <filesystem>
#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

namespace ManuscriptPath {

// Existing files have a physical identity, including through hard links. A
// missing Save As destination has only its pathname. In particular, two null
// canonicalFilePath() results say nothing about whether the names match.
inline bool same(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    if (a == b) return true;
    namespace fs = std::filesystem;
    const auto first = QFileInfo(a).filesystemAbsoluteFilePath();
    const auto second = QFileInfo(b).filesystemAbsoluteFilePath();
    std::error_code error;
    if (fs::equivalent(first, second, error)) return true;
    if (!error) return false; // Both names were inspected and identify different files.

    // Resolve all existing parents, including directory symlinks, then compare
    // the still-missing components. Never equate two failed resolutions.
    const auto firstName = fs::weakly_canonical(first, error);
    if (error || firstName.empty()) return false;
    const auto secondName = fs::weakly_canonical(second, error);
    if (error || secondName.empty()) return false;
    if (firstName == secondName) return true;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
#ifdef Q_OS_MACOS
    // APFS volumes can use either spelling policy. Query the existing parent
    // before treating case variants of a future filename as the same name.
    auto directory = firstName.parent_path();
    while (directory != directory.root_path() && !fs::exists(directory, error))
        directory = directory.parent_path();
    if (directory.empty() || pathconf(directory.c_str(), _PC_CASE_SENSITIVE) != 0) return false;
#endif
    return QFileInfo(firstName).filePath().compare(QFileInfo(secondName).filePath(), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

} // namespace ManuscriptPath
