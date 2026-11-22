#include "packagesmoke.h"
#include "documentstore.h"
#include "mainwindow.h"
#include "preferences.h"
#include "spellwords.h"
#include "textcompanion.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>

namespace {
int fail(const QString &message)
{
    qCritical().noquote() << "Crumb Writer package test:" << message;
    return 1;
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

template<typename Predicate> bool until(Predicate ready)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 10000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return ready();
}
}

int runPackageSmokeTest()
{
    QTemporaryDir directory;
    if (!directory.isValid()) return fail(directory.errorString());

    Preferences preferences(directory.filePath("settings.ini"));
    MainWindow window(preferences);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    if (!until([&] { return window.windowHandle() && window.windowHandle()->isExposed(); }))
        return fail("The Qt manuscript window could not be displayed.");
    if (QIcon(":/icons/crumbwriter.png").pixmap(32, 32).isNull())
        return fail("The manuscript icon could not be decoded.");
    if (!readBytes(":/license/LICENSE").contains("WITHOUT WARRANTY"))
        return fail("The offline software license is missing.");
    if (!SpellWords::available() || !SpellWords().issues(QStringLiteral("She’s writing in color")).isEmpty() ||
        SpellWords().issues("zzqzwrrp").isEmpty())
        return fail("The American English spelling dictionary is unusable.");
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (!SpellWords::usingBundledDictionary()) return fail("The packaged en_US dictionary is missing.");
#endif

    const QString manuscript = directory.filePath(QStringLiteral("Capítol 風.txt"));
    const QByteArray original = QByteArray::fromHex("efbbbf") + "Chapter one\r\n";
    QFile file(manuscript);
    if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) return fail(file.errorString());
    file.close();
    DocumentStore store;
    QString loaded, error;
    if (!store.open(manuscript, true, &loaded, &error)) return fail(error);
    if (loaded != QStringLiteral("Chapter one\n") || readBytes(store.backupPath()) != original)
        return fail("Unicode filename, newline decoding or byte-for-byte backup failed.");
    const QString revised = QStringLiteral("L’escriptor—風…\n");
    store.setSnapshotProvider([&] { return revised; });
    store.onError = [&](const QString &reason) { error = reason; };
    store.markEdited();
    store.save();
    if (!until([&] { return !store.isDirty() || !error.isEmpty(); }) || !error.isEmpty())
        return fail(error.isEmpty() ? QStringLiteral("Saving timed out.") : error);
    if (readBytes(manuscript) != QString(revised).replace(u'\n', "\r\n").toUtf8())
        return fail("The saved UTF-8 manuscript has incorrect bytes.");
    TextCompanion dictionary(".dictionary");
    const auto word = QStringLiteral("Cézanne\n").toUtf8();
    if (!dictionary.openManuscript(manuscript, &error) || !dictionary.write(word, &error)) return fail(error);
    if (readBytes(manuscript + ".dictionary") != word) return fail("Unicode sidecar round trip failed.");
    return 0;
}
