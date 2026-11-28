#include "backupcomparison.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "manuscriptoptions.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdlib>

namespace {
void check(bool value, const char *message)
{
    if (!value) qFatal("%s", message);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "open test file for reading");
    return file.readAll();
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open test file for editing");
    check(file.write(bytes) == bytes.size(), "write complete fixture");
}

void testDiff()
{
    using BackupDiff::compare;
    QStringList old{"a", "same", "same", "keep", "end"};
    QStringList now{"new", "same", "keep", "end", "added"};
    auto ranges = compare(old, now);
    for (auto it = ranges.crbegin(); it != ranges.crend(); ++it) {
        for (int i = 0; i < it->oldCount; ++i) old.removeAt(it->oldStart);
        for (int i = 0; i < it->newCount; ++i) old.insert(it->oldStart + i, now[it->newStart + i]);
    }
    check(old == now, "change ranges reconstruct the complete edited document");
    check(compare({}, {}).isEmpty(), "empty backups compare equal");
    QStringList large;
    for (int i = 0; i < 25092; ++i) large.append(QStringLiteral("Paragraph %1").arg(i));
    auto changed = large;
    changed[3] = "New introduction";
    changed[12546] = "Rewritten middle";
    changed.append("Coda");
    check(compare(large, changed).size() == 3, "distant edits in a full-length manuscript are separate passages");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testDiff();
    QTemporaryDir directory;
    check(directory.isValid(), "create manuscript directory");
    const QString path = directory.filePath("Novel.txt");
    const QByteArray initial = "Opening\nA paragraph.\n";
    write(path, initial);
    DocumentStore store;
    ManuscriptEditor editor;
    ManuscriptOptions options(&editor, &store, &editor);
    QString text, error;
    check(store.open(path, true, &text, &error), "open manuscript with a startup backup");
    editor.loadText(text);
    check(options.resetForPath(path, &error), "open missing optional settings");
    check(!QFile::exists(path + ".config"), "opening defaults leaves the directory uncluttered");
    write(path + ".config", "# author note\nfuture-setting=keep\ndisable-general-title=true\n");
    check(options.resetForPath(path, &error), "load future settings and author comment");
    editor.loadText("\n====\nBody.");
    options.setLineStyle(0);
    check(editor.manuscriptText() == "\n====\nBody.", "Regular Text preserves a literal marker after a blank line");
    editor.loadText("Explicit.\n=\nBody.");
    check(editor.model().paragraphRole(0) == ParagraphRole::DocumentTitle, "legacy sidecar cannot suppress an explicit heading");
    options.showSettings();
    auto *dialog = editor.findChild<QDialog *>("manuscriptSettingsDialog");
    auto *literal = dialog->findChild<QCheckBox *>("literalGuillemets");
    literal->setChecked(true);
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(editor.literalGuillemets(), "saving immediately applies literal interpretation");
    check(read(path + ".config").contains("future-setting=keep\n"), "unknown settings survive");
    const QString draft = directory.filePath("recovery.txt");
    check(options.copyToDraft(draft, &error) && read(draft + ".config") == read(path + ".config"),
          "a failed named save checkpoints interpretation alongside its recovery text");
    const QString copy = directory.filePath("Novel copy.txt");
    check(options.carryToPath(copy, &error), "save settings beside a new manuscript name");
    check(read(copy + ".config") == read(path + ".config"), "Save As preserves source settings and copies bytes");
    write(copy + ".config", "An external editor wrote this\n");
    options.showSettings();
    dialog = editor.findChild<QDialog *>("manuscriptSettingsDialog");
    dialog->findChild<QCheckBox *>("literalGuillemets")->setChecked(false);
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    check(dialog->isVisible() && read(copy + ".config") == "An external editor wrote this\n", "conflicting settings survive a failed save");
    dialog->reject();
    editor.loadText("Opening\nA revised paragraph.\n");
    options.showBackupComparison();
    auto *comparison = editor.findChild<QDialog *>("backupComparisonDialog");
    check(comparison && comparison->findChild<QListWidget *>("backupChanges")->count() == 1, "dialog shows the changed passage");
    const auto panes = comparison->findChildren<QPlainTextEdit *>();
    check(panes.size() == 2 && panes[0]->isReadOnly() && panes[1]->isReadOnly(), "comparison panes are read-only");
    check(read(store.backupPath()) == initial && read(path) == initial, "comparison preserves both files");
    return EXIT_SUCCESS;
}
