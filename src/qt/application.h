#ifndef CRUMBWRITER_APPLICATION_H
#define CRUMBWRITER_APPLICATION_H

#include "preferences.h"
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <memory>

class MainWindow;

class WriterApplication final : public QObject {
public:
    explicit WriterApplication(QObject *parent = nullptr);
    enum class Launch { Primary, Forwarded, Error };
    Launch start(const QStringList &files);
    void activate(const QStringList &files);
private:
    MainWindow *newWindow();
    MainWindow *preferredWindow() const;
    void acceptRequests();
    Preferences preferences_;
    QList<QPointer<MainWindow>> windows_;
    QPointer<MainWindow> lastActive_;
    bool recovered_ = false;
    QLocalServer server_;
    std::unique_ptr<QLockFile> instanceLock_;
};

#endif
