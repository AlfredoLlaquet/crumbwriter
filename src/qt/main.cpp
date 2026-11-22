#include "application.h"
#include "buildinfo.h"
#include "packagesmoke.h"

#include <QApplication>
#include <QDir>
#include <QFileOpenEvent>
#include <QIcon>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif
#include <clocale>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {
class DesktopApplication final : public QApplication {
public:
    using QApplication::QApplication;
    QStringList initialFiles;
    WriterApplication *session = nullptr;

    bool event(QEvent *event) override
    {
        if (event->type() != QEvent::FileOpen) return QApplication::event(event);
        const auto *open = static_cast<QFileOpenEvent *>(event);
        if (!open->url().isLocalFile()) return false;
        const QString file = QDir::current().absoluteFilePath(open->url().toLocalFile());
        if (session) session->activate({file});
        else initialFiles.append(file);
        return true;
    }
};

#ifdef Q_OS_WIN
struct InstallerMutex {
    // Setup and Uninstall use this marker to request a normal close before
    // replacing an executable or DLL. The kernel releases it after a crash.
    HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\org.thecrumbs.CrumbWriter.Running");
    ~InstallerMutex() { if (handle) CloseHandle(handle); }
};
#endif
}

int main(int argc, char **argv)
{
    std::setlocale(LC_ALL, "");
    for (int i = 1; i < argc && std::strcmp(argv[i], "--"); ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("Crumb Writer %s\n", CrumbWriter::version);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::puts("Usage: crumbwriter [--version | --smoke-test] [--] [MANUSCRIPT ...]");
            return 0;
        }
    }
    QCoreApplication::setOrganizationName("TheCrumbs");
    QCoreApplication::setApplicationName("CrumbWriter");
    QCoreApplication::setApplicationVersion(CrumbWriter::version);
#ifdef Q_OS_MACOS
    // Preserve the physical Control-based manuscript commands. Qt's standard
    // editing keys and native menu shortcuts still use physical Command.
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    DesktopApplication app(argc, argv);
#ifdef Q_OS_WIN
    const InstallerMutex installerMutex;
    std::setlocale(LC_CTYPE, ".UTF8");
#else
    std::setlocale(LC_CTYPE, "");
#endif
    QGuiApplication::setApplicationDisplayName("Crumb Writer");
    QGuiApplication::setDesktopFileName("org.thecrumbs.CrumbWriter");
    QIcon icon(":/icons/crumbwriter.png");
    for (int size : {16, 24, 32, 48, 64, 128}) icon.addFile(QString(":/icons/%1.png").arg(size));
    QApplication::setWindowIcon(QIcon::fromTheme("org.thecrumbs.CrumbWriter", icon));

    if (app.arguments().mid(1) == QStringList{QStringLiteral("--smoke-test")})
        return runPackageSmokeTest();

    QStringList files;
    bool pathsOnly = false;
    for (const auto &arg : app.arguments().mid(1)) {
        if (!pathsOnly && arg == "--") pathsOnly = true;
        else if (!pathsOnly && arg.startsWith('-')) {
            std::fprintf(stderr, "Unknown option: %s\n", qPrintable(arg));
            return 2;
        } else files.append(QDir::current().absoluteFilePath(arg));
    }
    // Cocoa sends documents opened by Finder and dropped on the Dock as
    // QFileOpenEvents, including during startup before the window exists.
#ifdef Q_OS_MACOS
    app.processEvents();
#endif
    files.append(app.initialFiles);
    files.removeDuplicates();
    WriterApplication session;
    const auto result = session.start(files);
    if (result == WriterApplication::Launch::Forwarded) return 0;
    if (result == WriterApplication::Launch::Error) return 1;
    app.session = &session;
    // Open events can also arrive during window creation.
    for (const auto &file : std::as_const(app.initialFiles))
        if (!files.contains(file)) session.activate({file});
    app.initialFiles.clear();
    const int status = app.exec();
    app.session = nullptr;
    return status;
}
