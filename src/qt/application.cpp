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
    checkpoint_.setSingleShot(true);
    checkpoint_.setInterval(600);
    connect(&checkpoint_, &QTimer::timeout, this, [this] { saveWorkspace(); });
    preferences_.changed = [this] {
        for (const auto &window : windows_) if (window) window->applyPreferences();
    };
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *focused) {
        for (auto *owner = focused; owner; owner = owner->parentWidget()) {
            auto *window = dynamic_cast<MainWindow *>(owner);
            if (window && windows_.contains(window)) { lastActive_ = window; break; }
        }
    });
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

MainWindow *WriterApplication::preferredWindow() const
{
    const QWidget *active = QApplication::activeWindow();
    for (auto *owner = active; owner; owner = owner->parentWidget()) {
        for (const auto &window : windows_) if (window && window->isVisible() && owner == window) return window;
    }
    if (lastActive_ && lastActive_->isVisible()) return lastActive_;
    for (auto it = windows_.crbegin(); it != windows_.crend(); ++it)
        if (*it && (*it)->isVisible()) return *it;
    return nullptr;
}

MainWindow *WriterApplication::newWindow()
{
    auto *window = new MainWindow(preferences_);
    windows_.append(window);
    connect(window, &QObject::destroyed, this, [this] {
        windows_.removeIf([](const QPointer<MainWindow> &p) { return p.isNull(); });
    });
    window->makeWindow = [this] { return newWindow(); };
    window->workspaceChanged = [this, window](bool closing) {
        if (restoring_) return;
        if (closing) {
            checkpoint_.stop();
            saveWorkspace(window);
            if (quitting_) {
                closedDuringQuit_.append(window->workspaceState());
                QTimer::singleShot(0, this, &WriterApplication::closeNextWindow);
            }
        } else if (!quitting_ && !checkpoint_.isActive()) checkpoint_.start();
    };
    window->quitApplication = [this] {
        if (!quitting_) { quitting_ = true; closedDuringQuit_.clear(); closeNextWindow(); }
    };
    window->quitCanceled = [this] { quitting_ = false; closedDuringQuit_.clear(); };
    return window;
}

void WriterApplication::closeNextWindow()
{
    if (!quitting_) return;
    if (auto *window = preferredWindow()) window->close();
}

void WriterApplication::saveWorkspace(MainWindow *closing)
{
    if (!preferences_.enabled("reopen-last")) return;
    QJsonArray layout;
    for (const auto &state : closedDuringQuit_) layout.append(state);
    for (const auto &window : windows_) {
        if (window && window->isVisible() && (window != closing || quitting_))
            layout.append(window->workspaceState());
    }
    if (layout.isEmpty() && closing) layout.append(closing->workspaceState());
    if (!layout.isEmpty()) {
        preferences_.settings().setValue("workspace/windows", QJsonDocument(layout).toJson(QJsonDocument::Compact));
        preferences_.settings().sync();
    }
}

void WriterApplication::activate(const QStringList &files)
{
    const bool coldStart = !recovered_;
    const QString last = preferences_.lastDocument();
    MainWindow *target = preferredWindow();
    auto destination = [&]() {
        if (!target) target = newWindow();
        return target;
    };
    if (coldStart) {
        recovered_ = true;
        restoring_ = true;
        QStringList missing;
        const auto layout = QJsonDocument::fromJson(preferences_.settings().value("workspace/windows").toByteArray()).array();
        if (preferences_.enabled("reopen-last")) {
            for (const auto &value : layout) {
                target = newWindow();
                missing += target->restoreWorkspace(value.toObject());
            }
        }
        for (const auto &draft : DocumentStore::recoveryDrafts())
            if (!MainWindow::findOpen(draft)) destination()->openDocumentInTab(draft, true);
        if (!missing.isEmpty()) destination()->showNotice(tr("Could not reopen: %1").arg(missing.join(", ")));
        restoring_ = false;
    }
    if (!files.isEmpty()) {
        for (const QString &path : files) destination()->openDocumentInTab(path);
    } else if (coldStart && !target && preferences_.enabled("reopen-last") && !last.isEmpty()) {
        destination()->openDocumentInTab(last);
    } else {
        destination()->activateManuscript(false);
    }
}
