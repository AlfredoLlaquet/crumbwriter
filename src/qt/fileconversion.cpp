#include "mainwindow.h"
#include "documentformats.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "manuscriptpath.h"
#include "manuscriptsession.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

namespace {
using DocumentFormats::Format;
struct Choice { Format format; const char *label; const char *suffix; bool output; };
const Choice choices[] = {
    {Format::Docx, "Word document (*.docx)", "docx", true},
    {Format::Odt, "OpenDocument text (*.odt)", "odt", true},
    {Format::Pdf, "PDF document (*.pdf)", "pdf", true},
    {Format::Html, "HTML document (*.html *.htm)", "html", true},
    {Format::Markdown, "Markdown (*.md *.markdown)", "md", false},
    {Format::Rtf, "Rich Text Format (*.rtf)", "rtf", false},
    {Format::Text, "Plain text (*.txt)", "txt", true},
    {Format::FixedText, "Fixed-width text (*.txt)", "txt", true}
};

const Choice *chooseFile(QWidget *owner, bool saving, QString *path)
{
    QFileDialog picker(owner, saving ? QObject::tr("Export a copy") : QObject::tr("Import a document"));
    picker.setAcceptMode(saving ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    picker.setFileMode(saving ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    QStringList filters;
    if (!saving) filters << QObject::tr("Supported documents (*.docx *.odt *.pdf *.html *.htm *.md *.markdown *.rtf *.txt)");
    for (const auto &choice : choices) if (!saving || choice.output) filters << choice.label;
    picker.setNameFilters(filters);
    if (saving) {
        picker.setDefaultSuffix(choices[0].suffix);
        QObject::connect(&picker, &QFileDialog::filterSelected, &picker, [&picker](const QString &filter) {
            for (const auto &entry : choices) if (filter == entry.label) picker.setDefaultSuffix(entry.suffix);
        });
    }
    if (picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty()) return nullptr;
    *path = picker.selectedFiles().first();
    for (const auto &choice : choices) if (picker.selectedNameFilter() == choice.label) return &choice;
    const auto suffix = QFileInfo(*path).suffix().toLower();
    if (!suffix.isEmpty()) for (const auto &choice : choices) {
        if (QString::fromLatin1(choice.label).contains("*." + suffix + ")") ||
            QString::fromLatin1(choice.label).contains("*." + suffix + " ")) return &choice;
    }
    return &choices[6];
}

bool textOptions(QWidget *parent, const QByteArray &bytes, bool importing, bool fixed, DocumentFormats::TextOptions &options)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Text conversion"));
    auto *form = new QFormLayout(&dialog);
    auto *encoding = new QComboBox(&dialog);
    encoding->setEditable(true);
    encoding->addItems({"UTF-8", "UTF-16", "UTF-16LE", "UTF-16BE", "UTF-32", "Windows-1252", "ISO-8859-1",
                        "ISO-8859-15", "Windows-1250", "Windows-1251", "MacRoman", "CP437", "CP850", "Shift_JIS"});
    if (bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff")) encoding->setCurrentText("UTF-16");
    if (bytes.startsWith(QByteArray::fromHex("fffe0000")) || bytes.startsWith(QByteArray::fromHex("0000feff")))
        encoding->setCurrentText("UTF-32");
    form->addRow(QObject::tr("Encoding"), encoding);
    auto *columns = new QSpinBox(&dialog);
    columns->setRange(1, 10000);
    columns->setValue(80);
    if (fixed && importing) form->addRow(QObject::tr("Source line width"), columns);
    else columns->hide();
    auto *preview = new QPlainTextEdit(&dialog);
    preview->setReadOnly(true);
    if (importing) form->addRow(QObject::tr("Preview"), preview);
    else preview->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto update = [=] {
        QString error;
        if (importing) {
            // Read only a sample and allow a multibyte character split at its boundary.
            QString sample;
            for (int trim = 0; trim <= 3 && trim <= bytes.size(); ++trim) {
                error.clear();
                sample = DocumentFormats::decode(bytes.left(bytes.size()-trim), encoding->currentText(), &error);
                if (error.isEmpty()) break;
            }
            preview->setPlainText(error.isEmpty() ? sample : error);
        }
        buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty() && !encoding->currentText().isEmpty());
    };
    QObject::connect(encoding, &QComboBox::currentTextChanged, &dialog, update);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted) return false;
    options.encoding = encoding->currentText();
    options.columns = columns->value();
    return true;
}
}

void MainWindow::importDocument()
{
    QString source, error;
    const auto *choice = chooseFile(this, false, &source);
    if (!choice) return;
    DocumentFormats::TextOptions options;
    if (choice->format == Format::Text || choice->format == Format::FixedText) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly)) { showNotice(file.errorString()); return; }
        if (!textOptions(this, file.read(8192), true, choice->format == Format::FixedText, options)) return;
    }
    const auto text = DocumentFormats::importFile(source, choice->format, options, &error);
    if (!error.isEmpty()) { showNotice(error); return; }
    newTab();
    editor()->loadText(text);
    active_->store_->markEdited();
    editor()->jumpTo(0);
    showNotice(tr("Imported %1 into a new manuscript.").arg(QFileInfo(source).fileName()));
}

void MainWindow::exportDocument()
{
    QString destination, error;
    const auto *choice = chooseFile(this, true, &destination);
    if (!choice) return;
    if (findOpen(destination)) { showNotice(tr("Choose a destination separate from your open manuscripts.")); return; }
    DocumentFormats::TextOptions options;
    if ((choice->format == Format::Text || choice->format == Format::FixedText) &&
        !textOptions(this, {}, false, choice->format == Format::FixedText, options)) return;
    editor()->finishTyping();
    if (!DocumentFormats::exportFile(destination, choice->format, options, editor(), &error)) showNotice(error);
    else showNotice(tr("Exported %1.").arg(destination));
}
