#include "application.h"
#include "documentstore.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

namespace {
QString resolvedPath(const QString &path)
{
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? file.absoluteFilePath() : canonical;
}

struct ActivationPaths {
    QString lockFile;
    QString endpoint;
};

ActivationPaths activationPaths()
{
#ifdef Q_OS_WIN
    const QString location = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QDir directory(location);
    if (location.isEmpty() || !directory.mkpath(".")) return {};
    // Windows named pipes take a name, not a filesystem path. Include the
    // profile in that name so unrelated local users can run independent editors.
    const auto profile = QCryptographicHash::hash(directory.absolutePath().toCaseFolded().toUtf8(),
                                                   QCryptographicHash::Sha256).toHex();
    return {directory.filePath("activation.lock"),
            QStringLiteral("org.thecrumbs.CrumbWriter.") + QString::fromLatin1(profile)};
#elif defined(Q_OS_MACOS)
    // Darwin supplies a short, private per-user temp directory. A socket in
    // ~/Library/Application Support can exceed sockaddr_un's path limit.
    const size_t size = confstr(_CS_DARWIN_USER_TEMP_DIR, nullptr, 0);
    if (!size) return {};
    QByteArray path(qsizetype(size), '\0');
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, path.data(), size) != size) return {};
    const QDir directory(QFile::decodeName(path.constData()));
    const QString endpoint = directory.filePath("org.thecrumbs.CrumbWriter.socket");
    return {endpoint + ".lock", endpoint};
#else
    const QDir directory(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
    const QString endpoint = directory.filePath("org.thecrumbs.CrumbWriter.socket");
    return {endpoint + ".lock", endpoint};
#endif
}
}

WriterApplication::WriterApplication(QObject *parent) : QObject(parent)
{
    preferences_.changed = [this] {
        for (const auto &window : windows_) if (window) window->applyPreferences();
    };
    connect(&server_, &QLocalServer::newConnection, this, &WriterApplication::acceptRequests);
}

WriterApplication::Launch WriterApplication::start(const QStringList &files)
{
    const auto paths = activationPaths();
    if (paths.lockFile.isEmpty()) {
        qWarning("The desktop runtime directory could not be opened for activation.");
        activate(files);
        return Launch::Primary;
    }
    const QString &endpoint = paths.endpoint;
    instanceLock_ = std::make_unique<QLockFile>(paths.lockFile);
    instanceLock_->setStaleLockTime(0);
    if (instanceLock_->tryLock()) {
        // This process owns the lifetime lock. An endpoint left by a crash is safe to remove.
        QLocalServer::removeServer(endpoint);
        server_.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server_.listen(endpoint)) {
            qWarning("Single-instance activation unavailable: %s", qPrintable(server_.errorString()));
            instanceLock_.reset();
        }
    } else if (instanceLock_->error() == QLockFile::LockFailedError) {
        QLocalSocket socket;
        QElapsedTimer elapsed;
        elapsed.start();
        do {
            socket.abort();
            socket.connectToServer(endpoint);
            if (socket.waitForConnected(200)) break;
            QThread::msleep(25);
        } while (elapsed.elapsed() < 3000);
        if (socket.state() != QLocalSocket::ConnectedState) {
            qWarning("The running Crumb Writer could not be reached: %s", qPrintable(socket.errorString()));
            return Launch::Error;
        }
        auto request = QJsonDocument(QJsonArray::fromStringList(files)).toJson(QJsonDocument::Compact);
        request += '\n';
        socket.write(request);
        while (socket.bytesToWrite() && socket.waitForBytesWritten(3000)) {}
        if (socket.bytesToWrite() || (!socket.bytesAvailable() && !socket.waitForReadyRead(10000)))
            return Launch::Error;
        return socket.readLine() == "ok\n" ? Launch::Forwarded : Launch::Error;
    } else {
        qWarning("The desktop runtime directory could not be locked for activation.");
    }
    activate(files);
    return Launch::Primary;
}

void WriterApplication::acceptRequests()
{
    while (auto *socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        auto receive = [this, socket] {
            if (!socket->canReadLine()) return;
            const auto message = QJsonDocument::fromJson(socket->readLine());
            QStringList files;
            if (message.isArray()) {
                for (const auto &item : message.array()) if (item.isString()) files.append(item.toString());
                activate(files);
                socket->write("ok\n");
            } else {
                socket->write("error\n");
            }
            socket->disconnectFromServer();
        };
        connect(socket, &QLocalSocket::readyRead, socket, receive);
        receive();
    }
}

MainWindow *WriterApplication::findWindow(const QString &path) const
{
    const auto resolved = resolvedPath(path);
    for (const auto &window : windows_) {
        if (window && !window->path().isEmpty() &&
            (window->path() == resolved || QFileInfo(window->path()) == QFileInfo(resolved)))
            return window;
    }
    return nullptr;
}

MainWindow *WriterApplication::newWindow()
{
    auto *window = new MainWindow(preferences_);
    windows_.append(window);
    connect(window, &QObject::destroyed, this, [this] {
        windows_.removeIf([](const QPointer<MainWindow> &p) { return p.isNull(); });
    });
    window->openRequested = [this](const QString &file) { openPath(file); };
    window->newRequested = [this] { newWindow()->activateManuscript(false); };
    window->pathAvailable = [this, window](const QString &path) {
        auto *existing = findWindow(path);
        return !existing || existing == window;
    };
    return window;
}

void WriterApplication::openPath(const QString &path)
{
    if (auto *existing = findWindow(path)) {
        existing->activateManuscript(true);
    } else {
        auto *window = newWindow();
        window->loadDocument(path);
        window->activateManuscript(false);
    }
}

void WriterApplication::activate(const QStringList &files)
{
    const bool coldStart = !recovered_;
    if (coldStart) {
        recovered_ = true;
        for (const QString &draft : DocumentStore::recoveryDrafts()) {
            auto *window = newWindow();
            window->loadDocument(draft, true);
            window->activateManuscript(false);
        }
    }
    if (!files.isEmpty()) {
        for (const QString &path : files) openPath(path);
    } else if (coldStart && preferences_.enabled("reopen-last") && !preferences_.lastDocument().isEmpty()) {
        openPath(preferences_.lastDocument());
    } else {
        QWidget *active = QApplication::activeWindow();
        for (const auto &window : windows_) {
            if (window && (window == active || (active && window->isAncestorOf(active)))) {
                window->activateManuscript(false);
                return;
            }
        }
        if (auto *last = findWindow(preferences_.lastDocument())) last->activateManuscript(false);
        else if (!windows_.isEmpty() && windows_.last()) windows_.last()->activateManuscript(false);
        else newWindow()->activateManuscript(false);
    }
}
