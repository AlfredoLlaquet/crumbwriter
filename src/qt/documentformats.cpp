#include "documentformats.h"
#include "genericparagraph.h"
#include "manuscripteditor.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QPdfWriter>
#include <QProcess>
#include <QRawFont>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextDocument>
#include <QtEndian>
#include <glib.h>

namespace DocumentFormats {
namespace {
QByteArray convert(const QByteArray &input, const QByteArray &from, const QByteArray &to, QString *error)
{
    GError *failure = nullptr;
    gsize size = 0;
    char *result = g_convert(input.constData(), input.size(), to.constData(), from.constData(), nullptr, &size, &failure);
    if (!result) {
        *error = QObject::tr("Cannot convert %1 to %2. %3").arg(QString::fromLatin1(from), QString::fromLatin1(to),
                                                                QString::fromUtf8(failure->message));
        g_error_free(failure);
        return {};
    }
    QByteArray bytes(result, size);
    g_free(result);
    return bytes;
}

QString reflow(const QString &text, int width)
{
    static const QRegularExpression item(R"(^\s*(?:[-*]|\(\*\)|\(?[0-9A-Za-z]+[.)])\s)");
    QStringList paragraphs;
    QString previous;
    for (const auto &line : text.split(u'\n')) {
        const auto trimmed = line.trimmed();
        const auto word = trimmed.section(u' ', 0, 0);
        const bool wraps = !previous.isEmpty() && !trimmed.isEmpty() &&
            (previous.size() + 1 + word.size() > width || previous.size() >= width - 1);
        if (wraps && !line.front().isSpace() && !item.match(line).hasMatch() &&
            !GenericParagraph::markerLevel(line) && !GenericParagraph::markerLevel(previous))
            paragraphs.last() += u' ' + trimmed;
        else paragraphs.append(line);
        previous = trimmed;
    }
    return paragraphs.join(u'\n');
}

QString fixedWidth(QString text, int width)
{
    text.replace(QRegularExpression(R"(\. (?! ))"), QStringLiteral(".  "));
    QStringList output;
    for (const auto &paragraph : text.split(u'\n')) {
        QString row;
        int column = 0, lastSpace = -1;
        QTextBoundaryFinder chars(QTextBoundaryFinder::Grapheme, paragraph);
        int start = 0;
        for (int next; (next = chars.toNextBoundary()) >= 0; start = next) {
            const auto glyph = paragraph.mid(start, next - start);
            if (column == width) {
                if (glyph == " ") {
                    output << row.trimmed();
                    row.clear(); column = 0; lastSpace = -1;
                    continue;
                }
                if (lastSpace >= 0) {
                    output << row.left(lastSpace).trimmed();
                    row = row.mid(lastSpace + 1);
                    column = row.isEmpty() ? 0 : int(GenericParagraph::markerLength(row));
                } else { output << row; row.clear(); column = 0; }
                lastSpace = -1;
            }
            if (glyph == " ") lastSpace = row.size();
            row += glyph;
            ++column;
        }
        output << row;
    }
    return output.join(u'\n');
}

bool canEmbed(const QFont &font)
{
    const auto table = QRawFont::fromFont(font).fontTable("OS/2");
    if (table.size() < 10) return true;
    // The PDF writer embeds subsets. Honor restricted, no-subset and bitmap-only flags.
    return !(qFromBigEndian<quint16>(table.constData() + 8) & (0x0002 | 0x0100 | 0x0200));
}
}

QString decode(const QByteArray &input, const QString &encoding, QString *error)
{
    auto text = QString::fromUtf8(convert(input, encoding.toLatin1(), "UTF-8", error));
    if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    return text.replace("\r\n", "\n").replace(u'\r', u'\n');
}

#ifndef Q_OS_MACOS
QString fromPdf(const QString &path, QString *error)
{
#ifdef Q_OS_WIN
    QString program = QCoreApplication::applicationDirPath() + "/pdftotext.exe";
    if (!QFileInfo::exists(program)) program = QStandardPaths::findExecutable("pdftotext");
#else
    const QString program = QStandardPaths::findExecutable("pdftotext");
#endif
    if (program.isEmpty()) { *error = QObject::tr("The pdftotext converter is unavailable."); return {}; }
    QProcess process;
    process.start(program, {"-enc", "UTF-8", "-nopgbrk", QFileInfo(path).absoluteFilePath(), "-"});
    if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(); }
        *error = QObject::tr("Could not extract PDF text. %1").arg(QString::fromLocal8Bit(process.readAllStandardError()));
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}
#endif

QString importFile(const QString &path, Format format, const TextOptions &options, QString *error)
{
    error->clear();
    if (format == Format::Docx || format == Format::Odt) return fromOffice(path, format == Format::Odt, error);
    if (format == Format::Pdf) {
        const auto text = fromPdf(path, error);
        if (text.trimmed().isEmpty() && error->isEmpty())
            *error = QObject::tr("This PDF has no extractable text. Image-only pages require OCR.");
        return text;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
    const auto bytes = file.readAll();
    if (format == Format::Rtf) return fromRtf(bytes, error);
    QTextDocument document;
    if (format == Format::Html) {
        const auto encoding = QStringConverter::encodingForHtml(bytes).value_or(QStringConverter::Utf8);
        document.setHtml(QStringDecoder(encoding).decode(bytes));
    } else if (format == Format::Markdown) document.setMarkdown(QString::fromUtf8(bytes));
    else {
        const auto text = decode(bytes, options.encoding, error);
        return format == Format::FixedText ? reflow(text, qMax(1, options.columns)) : text;
    }
    return fromRichText(document);
}

bool exportFile(const QString &path, Format format, const TextOptions &options, ManuscriptEditor *editor, QString *error)
{
    error->clear();
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly)) { *error = target.errorString(); return false; }
    bool ok = true;
    if (format == Format::Text || format == Format::FixedText) {
        QString text = editor->manuscriptText();
        if (format == Format::FixedText) text = fixedWidth(text, qMax(1, options.columns));
        if (!text.isEmpty() && !text.endsWith(u'\n')) text += u'\n';
        const auto bytes = convert(text.toUtf8(), "UTF-8", options.encoding.toLatin1(), error);
        ok = error->isEmpty() && target.write(bytes) == bytes.size();
    } else {
        auto doc = formattedDocument(editor);
        switch (format) {
        case Format::Html: {
            const auto html = doc->toHtml().toUtf8();
            ok = target.write(html) == html.size();
            break;
        }
        case Format::Docx: ok = writeDocx(&target, *doc, error); break;
        case Format::Odt: ok = writeOdt(&target, *doc, error); break;
        case Format::Pdf: {
            if (!canEmbed(doc->defaultFont())) {
                *error = QObject::tr("This font restricts embedding or subsetting. Select another font to export a PDF.");
                return false;
            }
            QPdfWriter pdf(&target);
            pdf.setCreator("Crumb Writer");
            pdf.setPageSize(QPageSize(QPageSize::A4));
            doc->print(&pdf);
            break;
        }
        default: ok = false;
        }
    }
    if (ok && target.commit()) return true;
    if (error->isEmpty()) *error = QObject::tr("Could not export %1. %2").arg(path, target.errorString());
    return false;
}
}
