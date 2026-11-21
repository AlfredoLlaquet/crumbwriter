#include "application.h"
#include "buildinfo.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <clocale>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    std::setlocale(LC_ALL, "");
    for (int i = 1; i < argc && std::strcmp(argv[i], "--"); ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("Crumb Writer %s\n", CrumbWriter::version);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::puts("Usage: crumbwriter [--version] [--] [MANUSCRIPT ...]");
            return 0;
        }
    }
    QCoreApplication::setOrganizationName("TheCrumbs");
    QCoreApplication::setApplicationName("CrumbWriter");
    QCoreApplication::setApplicationVersion(CrumbWriter::version);
    QApplication app(argc, argv);
    std::setlocale(LC_CTYPE, "");
    QGuiApplication::setApplicationDisplayName("Crumb Writer");
    QGuiApplication::setDesktopFileName("org.thecrumbs.CrumbWriter");
    QIcon icon(":/icons/crumbwriter.png");
    for (int size : {16, 24, 32, 48, 64, 128}) icon.addFile(QString(":/icons/%1.png").arg(size));
    QApplication::setWindowIcon(QIcon::fromTheme("org.thecrumbs.CrumbWriter", icon));

    QStringList files;
    bool pathsOnly = false;
    for (const auto &arg : app.arguments().mid(1)) {
        if (!pathsOnly && arg == "--") pathsOnly = true;
        else if (!pathsOnly && arg.startsWith('-')) {
            std::fprintf(stderr, "Unknown option: %s\n", qPrintable(arg));
            return 2;
        } else files.append(QDir::current().absoluteFilePath(arg));
    }
    WriterApplication session;
    const auto result = session.start(files);
    if (result == WriterApplication::Launch::Forwarded) return 0;
    if (result == WriterApplication::Launch::Error) return 1;
    return app.exec();
}
