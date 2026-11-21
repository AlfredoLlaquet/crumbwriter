#include "mainwindow.h"
#include "buildinfo.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

void MainWindow::showAbout()
{
    if (!about_) {
        auto *dialog = new QDialog(this);
        about_ = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("aboutDialog");
        dialog->setWindowTitle(tr("About Crumb Writer"));
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->setSpacing(10);
        auto *title = new QLabel(tr("Crumb Writer %1").arg(CrumbWriter::version), dialog);
        auto font = title->font();
        font.setBold(true);
        font.setPointSizeF(title->fontInfo().pointSizeF() * 1.3);
        title->setFont(font);
        layout->addWidget(title);
        layout->addWidget(new QLabel(tr("Programmed by ChatGPT for Alfredo Llaquet-Alsina."), dialog));
        layout->addWidget(new QLabel(tr("Qt %1 · The Unlicense").arg(qVersion()), dialog));

        auto *link = new QLabel(QStringLiteral("<a href=\"%1\">%1</a>").arg(CrumbWriter::repository), dialog);
        link->setObjectName("repositoryLink");
        link->setAccessibleName(tr("Crumb Writer GitHub repository"));
        link->setWordWrap(true);
        link->setTextInteractionFlags(Qt::TextBrowserInteraction);
        link->setFocusPolicy(Qt::StrongFocus);
        connect(link, &QLabel::linkActivated, dialog, [](const QString &url) {
            QDesktopServices::openUrl(QUrl(url));
        });
        // A label can receive initial focus before Qt selects its first anchor.
        auto *open = new QShortcut(link);
        open->setKeys({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
        open->setContext(Qt::WidgetShortcut);
        connect(open, &QShortcut::activated, link, [link] { link->linkActivated(CrumbWriter::repository); });
        layout->addWidget(link);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        link->setFocus(Qt::TabFocusReason);
    }
    about_->show();
    about_->raise();
    about_->activateWindow();
}
