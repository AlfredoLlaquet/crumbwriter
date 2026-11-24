#include "storage.h"
#include "qt/documentstore.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace {

void check(bool passed, const char *where, int line)
{
    if (!passed) {
        std::fprintf(stderr, "documentstore:%d: %s\n", line, where);
        std::exit(1);
    }
}
#define CHECK(condition) check(bool(condition), #condition, __LINE__)

void waitFor(const DocumentStore &store, const QString &error, const std::function<bool()> &done,
             const char *test, int line, const char *condition)
{
    if (done()) return;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(10);
    deadline.setSingleShot(true);
    deadline.setTimerType(Qt::PreciseTimer);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(10000);
    loop.exec();
    if (!done()) {
        const auto message = QStringLiteral("documentstore:%1:%2: timed out waiting for %3"
                                             "\n  status=%4, dirty=%5, saving=%6, path=%7, draft=%8, error=%9\n")
            .arg(QString::fromLatin1(test)).arg(line).arg(QString::fromLatin1(condition), store.status())
            .arg(store.isDirty()).arg(store.isSaving()).arg(store.path(), store.draftPath(), error);
        std::fputs(message.toUtf8().constData(), stderr);
        std::exit(1);
    }
}
#define WAIT_FOR(store, error, condition) \
    waitFor(store, error, [&] { return bool(condition); }, __func__, __LINE__, #condition)

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(bytes) == bytes.size());
    CHECK(file.flush());
}

QByteArray contents(const QString &path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

struct Fixture {
    Fixture(const QString &path, const QByteArray &bytes) : path(path)
    {
        writeFile(path, bytes);
        CHECK(store.open(path, true, &text, &error));
        store.setSnapshotProvider([this] {
            CHECK(QThread::currentThread() == QCoreApplication::instance()->thread());
            ++snapshots;
            return text;
        });
        store.captureMetadata = [this] { return text.toUtf8(); };
        store.onPublished = [this](const QString &name, const QByteArray &metadata) {
            CHECK(QThread::currentThread() == QCoreApplication::instance()->thread());
            published.append({name, metadata});
        };
        store.configureLiveSave(false, 1);
        store.onReadyToClose = [this] { ++closures; };
        store.onError = [this](const QString &failure) { error = failure; };
    }

    void edit(const QString &value) { text = value; store.markEdited(); }

    QString path, text, error;
    int snapshots = 0, closures = 0;
    QList<std::pair<QString, QByteArray>> published;
    DocumentStore store;
};

void datedBackupAndLatestClose(const QString &path)
{
    const QByteArray original = QByteArray::fromHex("efbbbf") + "Crumb 1: t: A \r\nCrumb 2: t: B\r";
    Fixture f(path, original);
    CHECK(f.text == "Crumb 1: t: A\nCrumb 2: t: B\n");
    CHECK(contents(f.store.backupPath()) == original);
    CHECK(!f.store.needsCloseConfirmation());
    f.edit("Crumb 1: t: Older\nCrumb 2: t: B\n");
    f.store.save();
    CHECK(f.store.isSaving());
    f.edit(QString::fromUtf8("Crumb 1: t: café\nCrumb 2: t: B"));
    f.store.save();
    f.store.save();
    f.store.beginCloseConfirmation();
    CHECK(f.store.needsCloseConfirmation());
    f.store.saveAndClose();
    WAIT_FOR(f.store, f.error, f.closures == 1);
    CHECK(!f.store.isSaving() && !f.store.isDirty());
    CHECK(f.snapshots == 2);
    CHECK(contents(path) == QByteArray("Crumb 1: t: café\r\nCrumb 2: t: B\r"));
    CHECK(contents(f.store.backupPath()) == original);
}

void oldSnapshotCannotMarkNewEditSaved(const QString &path)
{
    Fixture f(path, "one");
    f.edit("two");
    f.store.save();
    f.edit("three");
    WAIT_FOR(f.store, f.error, !f.store.isSaving());
    CHECK(contents(path) == "two\n");
    CHECK(f.store.isDirty() && f.store.needsCloseConfirmation());
    CHECK(f.published.size() == 1 && f.published[0].first == QFileInfo(path).canonicalFilePath() && f.published[0].second == "two");
    f.store.beginCloseConfirmation();
    f.store.discardAndClose();
    WAIT_FOR(f.store, f.error, f.closures == 1);
    CHECK(contents(path) == "two\n");
    CHECK(f.published.size() == 1);
}

void closeWithNewerQueuedEdit(const QString &path)
{
    Fixture f(path, "old");
    f.edit("first write");
    f.store.save();
    f.edit("never publish this edit");
    f.store.save();
    f.store.beginCloseConfirmation();
    f.store.discardAndClose();
    WAIT_FOR(f.store, f.error, f.closures == 1);
    const QByteArray installed = contents(path);
    CHECK(installed == "old" || installed == "first write\n");
    CHECK(f.snapshots == 1);
    CHECK(f.published.size() == (installed == "old" ? 0 : 1));
    if (!f.published.isEmpty()) CHECK(f.published[0].second + '\n' == installed);
}

void guardedDiscardBeforePublication(const QString &path)
{
    writeFile(path, "baseline");
    const QByteArray localName = QFile::encodeName(path);
    char *text = nullptr;
    GError *error = nullptr;
    CwFile *file = cw_file_open(localName.constData(), false, &text, &error);
    CHECK(file && text);
    g_free(text);
    CwWriteGate guard{};
    cw_write_gate_cancel(&guard);
    CHECK(!cw_file_save_guarded(file, "unsaved", &guard, &error));
    CHECK(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
    g_clear_error(&error);
    cw_file_free(file);
    CHECK(contents(path) == "baseline");
}

void closedDialogSuspendsDeadline(const QString &path)
{
    Fixture f(path, "old");
    f.store.configureLiveSave(true, 1);
    f.edit("initial text");
    f.store.beginCloseConfirmation();
    bool waited = false;
    QTimer::singleShot(1220, [&] { waited = true; });
    WAIT_FOR(f.store, f.error, waited);
    CHECK(contents(path) == "old" && f.snapshots == 0);
    f.store.keepEditing();

    // Continuous typing cannot postpone the first adopted deadline.
    QTimer input;
    int edits = 0;
    QObject::connect(&input, &QTimer::timeout, [&] {
        f.edit(QStringLiteral("ongoing %1").arg(++edits));
    });
    input.start(40);
    WAIT_FOR(f.store, f.error, !f.published.isEmpty() && f.published.last().second.startsWith("ongoing"));
    CHECK(contents(path).startsWith("ongoing"));
    input.stop();
    CHECK(edits > 0);
    f.store.beginCloseConfirmation();
    f.store.saveAndClose();
    WAIT_FOR(f.store, f.error, f.closures == 1);
    CHECK(contents(path) == f.text.toUtf8() + '\n');
}

void conflictRecoveryAndSaveAs(const QString &path)
{
    Fixture f(path, "original");
    f.edit("the author's new text");
    writeFile(path, "external changes");
    f.store.beginCloseConfirmation();
    f.store.saveAndClose();
    WAIT_FOR(f.store, f.error, !f.error.isEmpty() && !f.store.isSaving());
    CHECK(f.closures == 0 && f.store.isDirty());
    CHECK(contents(path) == "external changes");
    const QString draft = f.store.draftPath();
    CHECK(DocumentStore::recoveryDrafts().contains(draft));
    CHECK(contents(draft) == "the author's new text\n");
    CHECK(f.published.size() == 1 && f.published[0].first == draft && f.published[0].second + '\n' == contents(draft));
    const QString newName = path + ".copy";
    writeFile(newName, "the previous destination");
    int pathChanges = 0;
    f.store.onPathChanged = [&](const QString &oldPath, const QString &newPath) {
        CHECK(oldPath == QFileInfo(path).canonicalFilePath());
        CHECK(newPath == QFileInfo(newName).canonicalFilePath());
        ++pathChanges;
        // A bookmark-copy error can veto this close after the new file is safe.
        f.store.keepEditing();
    };
    f.store.saveAs(newName, true, true);
    f.edit("another change while Save As runs");
    WAIT_FOR(f.store, f.error, !f.store.isSaving());
    QCoreApplication::processEvents();
    CHECK(pathChanges == 1 && f.closures == 0 && f.store.isDirty());
    CHECK(contents(newName) == "the author's new text\n");
    CHECK(f.published.size() == 2 && f.published[1].first == QFileInfo(newName).canonicalFilePath() && f.published[1].second + '\n' == contents(newName));
    CHECK(contents(f.store.backupPath()) == "the previous destination");
    CHECK(!QFileInfo::exists(draft));
    f.store.saveAndClose();
    WAIT_FOR(f.store, f.error, f.closures == 1);
    CHECK(f.published.size() == 3 && f.published[2].first == QFileInfo(newName).canonicalFilePath() && f.published[2].second == f.text.toUtf8());
}

void closeDuringAutomaticSave(const QString &path)
{
    Fixture f(path, "original");
    f.store.configureLiveSave(true, 1);
    bool requested = false;
    f.store.onChanged = [&] {
        if (requested || !f.store.isSaving()) return;
        requested = true;
        // Dispatch notifies the GUI before its worker completion can run.
        // Canceling here can precede or follow the worker's atomic rename.
        CHECK(f.published.isEmpty());
        f.edit("final edit while the automatic save is in flight");
        f.store.beginCloseConfirmation();
        f.store.saveAndClose();
    };
    f.edit("automatic snapshot");
    WAIT_FOR(f.store, f.error, f.closures == 1);
    CHECK(requested && f.snapshots == 2 && !f.store.isDirty());
    CHECK(contents(path) == f.text.toUtf8() + '\n');
    CHECK(!f.published.isEmpty() && f.published.last().second + '\n' == contents(path));
}

void queuedSaveAsRespectsChosenDestination(const QString &source, bool originallyPresent)
{
    Fixture f(source, "original");
    const QString target = source + ".copy";
    if (originallyPresent)
        writeFile(target, "destination at confirmation");
    f.edit("finished manuscript");
    f.store.save();
    // The first job's completion cannot dispatch Save As until the GUI event
    // loop runs. Change its destination after the equivalent of choosing Save.
    f.store.saveAs(target, true);
    writeFile(target, "someone else's newer text");
    WAIT_FOR(f.store, f.error, !f.store.isSaving());
    CHECK(!f.error.isEmpty());
    CHECK(contents(target) == "someone else's newer text");
    CHECK(contents(source) == "finished manuscript\n" && f.store.path() == QFileInfo(source).canonicalFilePath());
    CHECK(f.store.draftPath().isEmpty() && !f.store.isDirty());
}

void untitledAndRecovered(const QString &target)
{
    QString draft;
    {
        DocumentStore untitled;
        QString error;
        untitled.onError = [&](const QString &failure) { error = failure; };
        untitled.setSnapshotProvider([] { return QString::fromUtf8("Recovered 🙂\t\tword  "); });
        untitled.markEdited();
        WAIT_FOR(untitled, error, !untitled.draftPath().isEmpty() && !untitled.isSaving());
        draft = untitled.draftPath();
        CHECK(untitled.isDirty() && contents(draft) == QByteArray("Recovered 🙂 word\n"));
        // An unplanned exit retains the draft.
    }
    CHECK(DocumentStore::recoveryDrafts().contains(draft));
    DocumentStore recovered;
    QString text, error;
    CHECK(recovered.openDraft(draft, &text, &error));
    CHECK(recovered.path().isEmpty() && recovered.needsCloseConfirmation());
    CHECK(text == QString::fromUtf8("Recovered 🙂 word\n"));
    recovered.onError = [&](const QString &failure) { error = failure; };
    recovered.setSnapshotProvider([&] { return text; });
    int choose = 0, closed = 0;
    recovered.onSaveAsNeeded = [&] { ++choose; };
    recovered.onReadyToClose = [&] { ++closed; };
    recovered.beginCloseConfirmation();
    recovered.saveAndClose();
    CHECK(choose == 1 && closed == 0);
    recovered.keepEditing(); // Cancel the first file chooser.
    recovered.beginCloseConfirmation();
    recovered.saveAndClose();
    CHECK(choose == 2);
    recovered.saveAs(target, false);
    WAIT_FOR(recovered, error, closed == 1);
    CHECK(contents(target) == text.toUtf8());
    CHECK(!QFileInfo::exists(draft));
}

void manuscriptOutput(const QString &path)
{
    Fixture f(path, "baseline");
    auto save = [&](const QString &live, const QByteArray &expected) {
        f.edit(live);
        f.store.save();
        WAIT_FOR(f.store, f.error, !f.store.isSaving());
        CHECK(f.error.isEmpty() && contents(path) == expected && f.text == live);
    };
    const QString title = QStringLiteral("e\u0301 👩‍🚀!");
    save(title + "\n-\nProse.\n\nSecond heading\n------------------\n\n",
         title.toUtf8() + "\n----\nProse.\n\nSecond heading\n--------------\n");
    save("Book//Leaf 1: A: Body.\n-\n\n", "Book//Leaf 1: A: Body.\n-\n");
    save("Read https://example.org\n-\nText.\n\n", "Read https://example.org\n------------------------\nText.\n");
    save("Heading\n  -\nBody.\n\t\n", "Heading\n -\nBody.\n");
    save("\n-\n-\nLast", "\n-\n-\nLast\n");
    save(" \t \n\n", {});
}

void rejectUnrepresentableSnapshot(const QString &path)
{
    Fixture f(path, "legible");
    f.edit(QString::fromLatin1("first\0second", 12));
    f.store.saveAndClose();
    WAIT_FOR(f.store, f.error, !f.store.isSaving());
    CHECK(f.closures == 0 && !f.error.isEmpty());
    CHECK(f.published.isEmpty());
    CHECK(contents(path) == "legible");
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir work;
    CHECK(work.isValid());
    qputenv("XDG_DATA_HOME", QFile::encodeName(work.filePath("private-data")));
    QCoreApplication application(argc, argv);
    datedBackupAndLatestClose(work.filePath(QStringLiteral("Títol 日本 🐦.txt")));
    oldSnapshotCannotMarkNewEditSaved(work.filePath("revisions.txt"));
    closeWithNewerQueuedEdit(work.filePath("discard-queued.txt"));
    guardedDiscardBeforePublication(work.filePath("cancel.txt"));
    closedDialogSuspendsDeadline(work.filePath("deadline.txt"));
    closeDuringAutomaticSave(work.filePath("automatic-close.txt"));
    conflictRecoveryAndSaveAs(work.filePath("conflict.txt"));
    queuedSaveAsRespectsChosenDestination(work.filePath("new-destination.txt"), false);
    queuedSaveAsRespectsChosenDestination(work.filePath("existing-destination.txt"), true);
    untitledAndRecovered(work.filePath("recovered.txt"));
    manuscriptOutput(work.filePath("canonical.txt"));
    rejectUnrepresentableSnapshot(work.filePath("invalid.txt"));
    std::puts("Document storage, live-save and close tests passed.");
}
