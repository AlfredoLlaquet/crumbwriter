#pragma once

#include <QDialog>
#include <QStringList>

class DocumentStore;
class ManuscriptEditor;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace BackupDiff {
struct Change {
    int oldStart, oldCount, newStart, newCount;
};
QList<Change> compare(const QStringList &before, const QStringList &after);
}

class BackupComparison final : public QDialog {
public:
    BackupComparison(ManuscriptEditor *, DocumentStore *);
    void refresh();
private:
    void display(int row);
    ManuscriptEditor *editor_;
    DocumentStore *store_;
    QLabel *summary_;
    QListWidget *changes_;
    QPlainTextEdit *beforeView_, *afterView_;
    QPushButton *previous_, *next_;
    QStringList before_, after_;
    QList<BackupDiff::Change> ranges_;
};
