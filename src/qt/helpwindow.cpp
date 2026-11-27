#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
QString contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
void addTopic(QListWidget *list, const QString &name, const QString &text, bool markdown)
{
    auto *item = new QListWidgetItem(name, list);
    item->setData(Qt::UserRole, text);
    item->setData(Qt::UserRole + 1, markdown);
}
void addLicenses(QListWidget *list)
{
    addTopic(list, QObject::tr("Credits and components"), contents(":/license/NOTICE.md"), true);
    addTopic(list, "Crumb Writer — The Unlicense", contents(":/license/LICENSE"), false);
    addTopic(list, "MyThes", contents(":/license/MyThes.txt"), false);
    addTopic(list, "Unicode", contents(":/license/Unicode.txt"), false);
    addTopic(list, "BSD first — CC0", contents(":/bsd-first/CC0"), false);
    const auto bin = QApplication::applicationDirPath();
    QStringList roots = {bin + "/third-party", bin + "/../Resources/third-party",
                         bin + "/../share/doc/crumbwriter/third-party"};
    for (const auto &root : roots) {
        QDirIterator files(root, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        QStringList paths;
        while (files.hasNext()) paths.append(files.next());
        paths.sort(Qt::CaseInsensitive);
        for (const auto &path : paths) {
            auto *item = new QListWidgetItem(QDir(root).relativeFilePath(path), list);
            item->setData(Qt::UserRole + 2, path);
        }
    }
}
void addHelp(QListWidget *list)
{
    const auto readme = contents(":/help/README.md");
    bool fenced = false;
    QString title, body;
    for (const auto &line : readme.split(u'\n')) {
        if (line.startsWith("~~~") || line.startsWith("\x60\x60\x60")) fenced = !fenced;
        if (!fenced && (line.startsWith("# ") || line.startsWith("## ") || line.startsWith("### "))) {
            if (!title.isEmpty()) addTopic(list, title, body, true);
            title = line.mid(line.indexOf(u' ') + 1);
            body.clear();
        }
        body += line + u'\n';
    }
    if (!title.isEmpty()) addTopic(list, title, body, true);
}
}

void MainWindow::showHelp(bool licenses)
{
    const QString id = licenses ? "licenseBrowser" : "helpBrowser";
    if (auto *open = findChild<QDialog *>(id)) { open->show(); open->raise(); open->activateWindow(); return; }
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(id);
    dialog->setWindowTitle(licenses ? tr("Licenses and credits") : tr("Crumb Writer Help"));
    auto *layout = new QVBoxLayout(dialog);
    auto *search = new QLineEdit(dialog);
    search->setPlaceholderText(tr("Filter topics"));
    search->setAccessibleName(tr("Filter topics"));
    layout->addWidget(search);
    auto *row = new QHBoxLayout;
    auto *list = new QListWidget(dialog);
    list->setAccessibleName(tr("Contents"));
    row->addWidget(list, 1);
    auto *reader = new QTextBrowser(dialog);
    reader->setOpenLinks(false);
    reader->setAccessibleName(tr("Selected topic"));
    row->addWidget(reader, 3);
    layout->addLayout(row);
    connect(list, &QListWidget::currentItemChanged, reader, [reader](QListWidgetItem *item) {
        if (!item) { reader->clear(); return; }
        const auto path = item->data(Qt::UserRole + 2).toString();
        auto text = path.isEmpty() ? item->data(Qt::UserRole).toString() : contents(path);
        if (item->data(Qt::UserRole + 1).toBool()) reader->setMarkdown(text);
        else reader->setPlainText(text);
    });
    connect(search, &QLineEdit::textChanged, list, [list](const QString &query) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            item->setHidden(!item->text().contains(query, Qt::CaseInsensitive));
            if (!first && !item->isHidden()) first = item;
        }
        list->setCurrentItem(first);
    });
    connect(reader, &QTextBrowser::anchorClicked, list, [list, search](const QUrl &url) {
        if (url.isRelative() && url.hasFragment()) {
            search->clear();
            for (int i = 0; i < list->count(); ++i) {
                auto slug = list->item(i)->text().toLower().remove(QRegularExpression("[^\\p{L}\\d -]")).replace(u' ', u'-');
                if (slug == url.fragment()) { list->setCurrentRow(i); return; }
            }
        } else if (url.scheme() == "https" || url.scheme() == "http") QDesktopServices::openUrl(url);
    });
    if (licenses) addLicenses(list); else addHelp(list);
    list->setCurrentRow(0);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->resize(840, 580);
    dialog->show();
}
