#ifndef CW_QT_DOCUMENTSTORE_H
#define CW_QT_DOCUMENTSTORE_H

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

// Called on the GUI thread. A worker owns all subsequent file I/O for a
// document. The snapshot provider is invoked once when a write is dispatched.
class DocumentStore final : public QObject {
public:
    explicit DocumentStore(QObject *parent = nullptr);
    ~DocumentStore() override;

    bool open(const QString &path, bool backup, QString *text, QString *error);
    bool openDraft(const QString &draftPath, QString *text, QString *error);
    static QStringList recoveryDrafts();
    void setSnapshotProvider(std::function<QString()> provider,
                             std::function<int()> provisionalMarker = {},
                             std::function<bool()> literalGuillemets = {});
    void markEdited();
    void configureLiveSave(bool live, int seconds);

    QString path() const;
    QString backupPath() const;
    QString draftPath() const;
    QString status() const;
    bool isDirty() const;
    quint64 editRevision() const;
    bool isSaving() const;
    bool needsCloseConfirmation() const;

    void save();
    void saveAs(const QString &path, bool backup, bool closeAfter = false);
    void beginCloseConfirmation();
    void keepEditing();
    void saveAndClose(bool keepDraft = false);
    bool draftIsCurrent() const;
    void discardAndClose();

    // A canceled Save As dialog calls keepEditing(). On Save As success,
    // onPathChanged runs before onReadyToClose; a sidecar failure may call
    // keepEditing() here to keep the document open.
    std::function<void()> onChanged;
    std::function<void(const QString &)> onError;
    std::function<void(const QString &oldPath, const QString &newPath)> onPathChanged;
    // Both hooks run on the GUI thread. Only an installed manuscript or draft
    // receives the metadata captured alongside its exact text revision.
    std::function<QByteArray()> captureMetadata;
    std::function<void(const QString &path, const QByteArray &metadata)> onPublished;
    std::function<void()> onSaveAsNeeded;
    std::function<void()> onReadyToClose;

private:
    bool load(const QString &path, bool backup, bool recovery, QString *text, QString *error);
    class State;
    std::unique_ptr<State> state;
};

#endif
