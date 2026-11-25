#include "storage.h"
#include "draft.h"
#include "documentstore.h"
#include "genericparagraph.h"
#include "manuscriptspacing.h"

extern "C" {
#include "crumb.h"
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <deque>
#include <utility>

namespace {

// Work on the worker's snapshot. A new blank writing row, its caret, undo
// history and live paragraph marks must survive even frequent autosaves.
QByteArray manuscriptBytes(const QString &snapshot, int provisionalMarker, bool literalGuillemets)
{
    QString manuscript = ManuscriptSpacing::normalizeInsertion(snapshot, false);
    qsizetype signatures = 0;
    while (signatures < manuscript.size() && manuscript.at(signatures) == QChar::ByteOrderMark)
        ++signatures;
    if (signatures) manuscript.remove(0, signatures);
    ManuscriptSpacing::trimBlankEnd(manuscript, true);
    if (manuscript.isEmpty()) return {};
    const QStringView text(manuscript);
    const auto firstLine = text.left(text.indexOf(u'\n'));
    if (firstLine.contains(QStringLiteral("//"))) {
        CwCrumb first{};
        cw_crumb_parse(firstLine.toUtf8().constData(), true, &first);
        const bool generic = first.record_start == 0;
        cw_crumb_clear(&first);
        if (!generic) return manuscript.toUtf8();
    }
    QString output;
    qsizetype copied = 0;
    QStringView previous;
    int lineNumber = 0;
    for (qsizetype start = 0; start < text.size(); ++lineNumber) {
        const auto end = text.indexOf(u'\n', start);
        const auto line = text.mid(start, end - start);
        if (lineNumber != provisionalMarker && GenericParagraph::markerLevel(line) &&
            GenericParagraph::canHaveMarker(previous)) {
            const auto length = GenericParagraph::markerLength(previous, literalGuillemets);
            if (length != line.size()) {
                output += text.mid(copied, start - copied);
                output += QString(length, line.front());
                copied = end;
            }
        }
        previous = line;
        start = end + 1;
    }
    if (!copied) return manuscript.toUtf8();
    output += text.mid(copied);
    return output.toUtf8();
}

enum class Operation { Save, SaveAs, Draft, Discard };
enum class Closing { No, Confirming, Saving, Discarding, Done };

struct Request {
    Operation operation = Operation::Save;
    QString destination;
    bool backup = false;
    bool automatic = false;
    std::shared_ptr<const CwTargetStamp> stamp = {};
};

struct Result {
    bool ok = false;
    bool canceled = false;
    bool recovered = false;
    QString error;
    QString path;
    QString backup;
    QString draft;
};

QString localPath(const char *name)
{
#ifdef Q_OS_WIN
    // GLib filenames are UTF-8 on Windows. Qt's encodeName/decodeName use
    // the legacy system code page there, which can lose manuscript names.
    return name ? QDir::fromNativeSeparators(QString::fromUtf8(name)) : QString();
#else
    return name ? QFile::decodeName(name) : QString();
#endif
}

QByteArray diskPath(const QString &name)
{
#ifdef Q_OS_WIN
    return name.toUtf8();
#else
    return QFile::encodeName(name);
#endif
}

QString takeError(GError *&error)
{
    QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("Unknown file error.");
    g_clear_error(&error);
    return message;
}

// After open, the UI reads only the path copies returned with a completed
// operation. The mutable disk identity, private draft and newline map have a
// single owner even when the UI accepts another edit during a write.
class DiskDocument {
public:
    ~DiskDocument()
    {
        cw_file_free(file);
        g_free(draft);
    }

    CwFile *file = nullptr;
    char *draft = nullptr;

    Result perform(const Request &request, const QString &snapshot, int provisionalMarker,
                   bool literalGuillemets, bool needRecovery,
                   CwWriteGate *gate)
    {
        Result result;
        GError *error = nullptr;
        if (request.operation == Operation::Discard) {
            if (!draft || !QFileInfo::exists(localPath(draft)) || QFile::remove(localPath(draft))) {
                g_clear_pointer(&draft, g_free);
                result.ok = true;
            } else {
                result.error = QStringLiteral("Could not remove the recovery draft %1.").arg(localPath(draft));
            }
        } else if (snapshot.contains(QChar::Null) || !snapshot.isValidUtf16()) {
            result.error = QStringLiteral("The manuscript contains a NUL or an invalid Unicode character.");
        } else {
            const QByteArray utf8 = manuscriptBytes(snapshot, provisionalMarker, literalGuillemets);
            switch (request.operation) {
            case Operation::Save:
                result.ok = cw_file_save_guarded(file, utf8.constData(), gate, &error);
                break;
            case Operation::SaveAs: {
                QFileInfo destination(request.destination);
                const QString source = localPath(cw_file_path(file));
                if (!source.isEmpty() && (source == destination.absoluteFilePath() ||
                                          destination == QFileInfo(source))) {
                    result.ok = cw_file_save_guarded(file, utf8.constData(), gate, &error);
                } else {
                    const QByteArray name = diskPath(request.destination);
                    CwFile *copy = cw_file_save_as_guarded(file, name.constData(), utf8.constData(),
                                                          request.backup, request.stamp.get(),
                                                          gate, &error);
                    if (copy) {
                        cw_file_free(file);
                        file = copy;
                        result.ok = true;
                    }
                }
                break;
            }
            case Operation::Draft:
                result.ok = cw_draft_write(&draft, utf8.constData(), &error);
                break;
            case Operation::Discard:
                break;
            }

            if (result.ok && request.operation != Operation::Draft) {
                cw_draft_remove(&draft);
            } else if (error) {
                result.canceled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
                result.error = takeError(error);
                if (!result.canceled && needRecovery && request.operation != Operation::Draft) {
                    result.recovered = cw_draft_write(&draft, utf8.constData(), &error);
                    if (result.recovered)
                        result.error += QStringLiteral(" A private recovery draft is saved.");
                    else
                        result.error += QStringLiteral(" Recovery draft: ") + takeError(error);
                }
            }
        }
        result.path = localPath(cw_file_path(file));
        result.backup = localPath(cw_file_backup_path(file));
        result.draft = localPath(draft);
        return result;
    }
};

} // namespace

class DocumentStore::State {
public:
    explicit State(DocumentStore *owner) : owner(owner)
    {
        workers.setMaxThreadCount(1);
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] {
            autoDue = true;
            pump();
        });
    }

    ~State()
    {
        timer.stop();
        cw_write_gate_cancel(gate.get());
        workers.waitForDone();
    }

    DocumentStore *owner;
    QTimer timer;
    QThreadPool workers;
    DiskDocument disk;
    std::shared_ptr<CwWriteGate> gate;
    std::function<QString()> snapshotProvider;
    std::function<int()> provisionalMarker;
    std::function<bool()> literalGuillemets;
    QString name, backup, draft, message;
    quint64 revision = 0;
    quint64 savedRevision = 0;
    quint64 draftRevision = 0;
    int interval = 1;
    bool live = true;
    bool manuscriptFailed = false;
    bool autoDue = false;
    bool awaitingName = false;
    bool activeAutomatic = false;
    Closing closing = Closing::No;
    std::deque<Request> pending;

    bool dirty() const { return savedRevision != revision; }
    bool busy() const { return bool(gate); }

    void notify()
    {
        if (owner->onChanged)
            owner->onChanged();
    }

    bool canOpen(QString *error) const
    {
        if (!busy() && !dirty() && pending.empty() && draft.isEmpty())
            return true;
        if (error)
            *error = QStringLiteral("Finish the current document before opening another in this window.");
        return false;
    }

    void reset(bool recovery)
    {
        timer.stop();
        autoDue = false;
        closing = Closing::No;
        awaitingName = false;
        manuscriptFailed = false;
        revision = recovery ? 1 : 0;
        savedRevision = 0;
        draftRevision = revision;
        name = localPath(cw_file_path(disk.file));
        backup = localPath(cw_file_backup_path(disk.file));
        draft = localPath(disk.draft);
        message = recovery ? QStringLiteral("Recovered draft") : QStringLiteral("Opened");
        notify();
    }

    void schedule()
    {
        if (!live || closing != Closing::No || !dirty() || timer.isActive() || autoDue)
            return;
        if ((!name.isEmpty() && !manuscriptFailed) || draftRevision != revision)
            timer.start(interval * 1000);
    }

    void needName()
    {
        if (!awaitingName && owner->onSaveAsNeeded) {
            awaitingName = true;
            owner->onSaveAsNeeded();
        }
    }

    void finishClose()
    {
        closing = Closing::Done;
        QMetaObject::invokeMethod(owner, [this] {
            if (closing == Closing::Done && owner->onReadyToClose)
                owner->onReadyToClose();
        }, Qt::QueuedConnection);
    }

    void dispatch(const Request &request)
    {
        const bool writing = request.operation != Operation::Discard;
        if (writing && !snapshotProvider) {
            closing = Closing::No;
            message = QStringLiteral("Could not read the manuscript for saving.");
            notify();
            if (owner->onError)
                owner->onError(message);
            return;
        }
        const quint64 captured = revision;
        // Qt's implicitly shared string can be read by the worker. Its UTF-8
        // conversion, normalization, conflict check, fsync and rename all run there.
        const QString snapshot = writing ? snapshotProvider() : QString();
        const int pendingMarker = writing && provisionalMarker ? provisionalMarker() : -1;
        const bool literalQuotes = writing && literalGuillemets && literalGuillemets();
        const QByteArray metadata = writing && owner->captureMetadata ? owner->captureMetadata() : QByteArray();
        const bool needRecovery = dirty() || request.operation == Operation::Save;
        auto writingGate = std::make_shared<CwWriteGate>();
        gate = writingGate;
        activeAutomatic = request.automatic;
        message = request.operation == Operation::Discard ? QStringLiteral("Discarding unsaved changes") :
                  request.operation == Operation::Draft ? QStringLiteral("Saving recovery draft") :
                                                         QStringLiteral("Saving");
        workers.start([this, request, snapshot, pendingMarker, literalQuotes, captured, metadata, writingGate, needRecovery] {
            Result result = disk.perform(request, snapshot, pendingMarker, literalQuotes, needRecovery, writingGate.get());
            QMetaObject::invokeMethod(owner, [this, request, captured, metadata, result = std::move(result)] {
                completed(request, captured, result, metadata);
            }, Qt::QueuedConnection);
        });
        notify();
    }

    void pump()
    {
        if (busy() || closing == Closing::Confirming || closing == Closing::Done)
            return;
        if (closing == Closing::Discarding) {
            if (draft.isEmpty())
                finishClose();
            else
                dispatch({Operation::Discard, {}, false, false});
            return;
        }
        if (!pending.empty()) {
            Request next = pending.front();
            pending.pop_front();
            if (next.operation == Operation::Save && name.isEmpty()) {
                needName();
                schedule();
            } else {
                timer.stop();
                autoDue = false;
                dispatch(next);
            }
            return;
        }
        if (closing == Closing::Saving) {
            if (name.isEmpty())
                needName();
            else if (dirty() || manuscriptFailed)
                dispatch({Operation::Save, {}, false, false});
            else
                finishClose();
            return;
        }
        if (autoDue) {
            autoDue = false;
            if (live && dirty()) {
                if (!name.isEmpty() && !manuscriptFailed)
                    dispatch({Operation::Save, {}, false, true});
                else if (draftRevision != revision)
                    dispatch({Operation::Draft, {}, false, true});
            }
        }
    }

    void completed(const Request &request, quint64 captured, const Result &result, const QByteArray &metadata)
    {
        gate.reset();
        const QString oldName = name;
        name = result.path;
        backup = result.backup;
        draft = result.draft;
        if (result.ok && request.operation == Operation::Discard) {
            finishClose();
            return;
        }
        if (result.ok && request.operation == Operation::Draft) {
            draftRevision = captured;
            message = name.isEmpty() ? QStringLiteral("Draft saved") :
                                       QStringLiteral("Recovery draft saved · manuscript save paused");
        } else if (result.ok) {
            savedRevision = captured;
            draftRevision = 0;
            manuscriptFailed = false;
            message = dirty() ? QStringLiteral("Unsaved changes") : QStringLiteral("Saved");
        } else if (result.canceled) {
            message = QStringLiteral("Unsaved changes");
        } else {
            if (result.recovered)
                draftRevision = captured;
            if (request.operation == Operation::Save)
                manuscriptFailed = true;
            message = request.operation == Operation::Draft ? QStringLiteral("Recovery draft failed") :
                                                              QStringLiteral("Save failed");
            if (closing == Closing::Saving || request.operation == Operation::Discard)
                closing = Closing::No;
        }
        const QString publishedPath = result.recovered || (result.ok && request.operation == Operation::Draft)
                                    ? result.draft : result.ok ? result.path : QString();
        if (oldName != name && owner->onPathChanged)
            owner->onPathChanged(oldName, name);
        if (!publishedPath.isEmpty() && owner->onPublished)
            owner->onPublished(publishedPath, metadata);
        notify();
        if (!result.ok && !result.canceled && closing != Closing::Discarding && owner->onError)
            owner->onError(result.error);
        pump();
    }

    void requestSave(Request request)
    {
        if (closing == Closing::Discarding || closing == Closing::Done)
            return;
        if (!pending.empty() && request.operation == pending.back().operation &&
            request.destination == pending.back().destination && request.backup == pending.back().backup)
            pending.back() = std::move(request);
        else
            pending.push_back(std::move(request));
        pump();
    }
};

DocumentStore::DocumentStore(QObject *parent) : QObject(parent), state(std::make_unique<State>(this)) {}
DocumentStore::~DocumentStore() = default;

bool DocumentStore::load(const QString &path, bool backup, bool recovery, QString *text, QString *error)
{
    if (!state->canOpen(error))
        return false;
    char *contents = nullptr;
    GError *failure = nullptr;
    const QByteArray filename = diskPath(path);
    CwFile *file = cw_file_open(filename.constData(), backup, &contents, &failure);
    if (!file) {
        const QString message = takeError(failure);
        if (error)
            *error = message;
        return false;
    }
    cw_file_free(state->disk.file);
    if (recovery) {
        state->disk.draft = g_strdup(filename.constData());
        cw_file_free(file);
        state->disk.file = nullptr;
    } else {
        state->disk.file = file;
    }
    if (text)
        *text = QString::fromUtf8(contents);
    g_free(contents);
    state->reset(recovery);
    return true;
}

bool DocumentStore::open(const QString &path, bool backup, QString *text, QString *error)
{
    return load(path, backup, false, text, error);
}

bool DocumentStore::openDraft(const QString &draftPath, QString *text, QString *error)
{
    return load(draftPath, false, true, text, error);
}

QStringList DocumentStore::recoveryDrafts()
{
    QStringList result;
    GPtrArray *paths = cw_draft_list();
    for (guint i = 0; i < paths->len; ++i)
        result.append(localPath(static_cast<const char *>(g_ptr_array_index(paths, i))));
    g_ptr_array_unref(paths);
    result.sort();
    return result;
}

void DocumentStore::setSnapshotProvider(std::function<QString()> provider,
                                        std::function<int()> provisionalMarker,
                                        std::function<bool()> literalGuillemets)
{
    state->snapshotProvider = std::move(provider);
    state->provisionalMarker = std::move(provisionalMarker);
    state->literalGuillemets = std::move(literalGuillemets);
}

void DocumentStore::markEdited()
{
    const bool wasDirty = isDirty();
    ++state->revision;
    if (!wasDirty) {
        if (!isSaving())
            state->message = QStringLiteral("Unsaved changes");
        state->notify();
    }
    state->schedule();
}

void DocumentStore::configureLiveSave(bool live, int seconds)
{
    seconds = std::clamp(seconds, 1, 30);
    if (live == state->live && seconds == state->interval)
        return;
    state->live = live;
    state->interval = seconds;
    state->timer.stop();
    state->autoDue = false;
    state->schedule();
}

QString DocumentStore::path() const { return state->name; }
QString DocumentStore::backupPath() const { return state->backup; }
QString DocumentStore::draftPath() const { return state->draft; }
QString DocumentStore::status() const { return state->message; }
bool DocumentStore::isDirty() const { return state->dirty(); }
quint64 DocumentStore::editRevision() const { return state->revision; }
bool DocumentStore::isSaving() const { return state->busy(); }
bool DocumentStore::needsCloseConfirmation() const
{
    return state->closing != Closing::Done && (isDirty() || isSaving() || state->manuscriptFailed ||
                                                !state->pending.empty());
}

void DocumentStore::save() { state->requestSave({Operation::Save, {}, false, false}); }

void DocumentStore::saveAs(const QString &path, bool backup, bool closeAfter)
{
    if (path.isEmpty() || state->closing == Closing::Discarding || state->closing == Closing::Done)
        return;
    state->awaitingName = false;
    auto stamp = std::make_shared<CwTargetStamp>();
    GError *failure = nullptr;
    if (!cw_file_stamp_target(diskPath(path).constData(), stamp.get(), &failure)) {
        state->closing = Closing::No;
        state->message = QStringLiteral("Save As failed");
        state->schedule();
        state->notify();
        const QString error = takeError(failure);
        if (onError)
            onError(error);
        return;
    }
    if (closeAfter)
        state->closing = Closing::Saving;
    state->requestSave({Operation::SaveAs, path, backup, false, std::move(stamp)});
}

void DocumentStore::beginCloseConfirmation()
{
    if (state->closing == Closing::Discarding || state->closing == Closing::Done)
        return;
    state->closing = Closing::Confirming;
    state->timer.stop();
    state->autoDue = false;
    if (state->activeAutomatic)
        cw_write_gate_cancel(state->gate.get());
}

void DocumentStore::keepEditing()
{
    state->closing = Closing::No;
    state->awaitingName = false;
    state->pump();
    state->schedule();
}

void DocumentStore::saveAndClose()
{
    if (state->closing == Closing::Discarding || state->closing == Closing::Done)
        return;
    state->closing = Closing::Saving;
    state->timer.stop();
    state->autoDue = false;
    state->pump();
}

void DocumentStore::discardAndClose()
{
    if (state->closing == Closing::Done)
        return;
    state->closing = Closing::Discarding;
    state->timer.stop();
    state->autoDue = false;
    state->pending.clear();
    cw_write_gate_cancel(state->gate.get());
    state->pump();
}
