#include "mainwindow.h"
#include "buildinfo.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

namespace {
class RepositoryLink final : public QLabel {
public:
    explicit RepositoryLink(QWidget *parent) : QLabel(parent) { updateLink(); }

protected:
    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::PaletteChange) updateLink();
    }

private:
    void updateLink()
    {
        // Rich-text anchors can retain the HTML renderer's default blue.
        setText(QStringLiteral("<a href=\"%1\" style=\"color:%2\">%1</a>")
                    .arg(CrumbWriter::repository, palette().link().color().name()));
    }
};

void showLicense(QDialog *about)
{
    auto *dialog = about->findChild<QDialog *>("licenseDialog", Qt::FindDirectChildrenOnly);
    if (!dialog) {
        dialog = new QDialog(about);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("licenseDialog");
        dialog->setWindowTitle(QObject::tr("Crumb Writer License — The Unlicense"));
        auto *layout = new QVBoxLayout(dialog);
        auto *text = new QPlainTextEdit(dialog);
        text->setAccessibleName(QObject::tr("Full license text"));
        text->setReadOnly(true);
        text->setTabChangesFocus(true);
        QFile license(":/license/LICENSE");
        if (license.open(QIODevice::ReadOnly | QIODevice::Text))
            text->setPlainText(QString::fromUtf8(license.readAll()));
        else
            text->setPlainText(QObject::tr("The license could not be read from this copy of Crumb Writer."));
        layout->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        QObject::connect(dialog, &QDialog::finished, about, [about] {
            if (about->isVisible()) about->activateWindow();
        });
        layout->addWidget(buttons);
        dialog->resize(QSize(660, 480).boundedTo(about->screen()->availableGeometry().size() * 0.85));
        text->setFocus(Qt::TabFocusReason);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
}

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
        layout->addWidget(new QLabel(tr("Qt %1").arg(qVersion()), dialog));
        auto *notice = new QLabel(tr("The Unlicense dedicates Crumb Writer’s original code to the "
                                     "public domain. The editor is provided “as is,” without "
                                     "warranty. Writers retain their rights to their manuscripts."), dialog);
        notice->setWordWrap(true);
        layout->addWidget(notice);

        auto *link = new RepositoryLink(dialog);
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
        auto *license = buttons->addButton(tr("View &License"), QDialogButtonBox::ActionRole);
        license->setObjectName("viewLicense");
        license->setShortcut(QKeySequence(QStringLiteral("Alt+L")));
        connect(license, &QPushButton::clicked, dialog, [dialog] { showLicense(dialog); });
        buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        link->setFocus(Qt::TabFocusReason);
    }
    about_->show();
    about_->raise();
    about_->activateWindow();
}
