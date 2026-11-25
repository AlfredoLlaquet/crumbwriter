#pragma once

#include "textcompanion.h"
#include <QObject>
#include <QPointer>
#include <functional>

class QDialog;
class DocumentStore;
class ManuscriptEditor;

// One controller travels with each manuscript, including an unnamed draft.
class ManuscriptOptions final : public QObject {
public:
    ManuscriptOptions(ManuscriptEditor *editor, DocumentStore *store, QObject *parent);
    bool resetForPath(const QString &path, QString *error = nullptr);
    bool carryToPath(const QString &path, QString *error = nullptr);
    bool flush(QString *error = nullptr);
    bool copyToDraft(const QString &draft, QString *error = nullptr);
    std::function<void()> onChanged;
    bool pending() const { return config_.hasPending(); }
    void showSettings();
    void showBackupComparison();

private:
    void apply();
    ManuscriptEditor *editor_;
    DocumentStore *store_;
    TextCompanion config_{QStringLiteral(".config")};
    TextCompanion recoveryConfig_{QStringLiteral(".config")};
    QPointer<QDialog> settings_, comparison_;
};
