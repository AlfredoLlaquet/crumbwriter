#pragma once
#include <QFont>
#include <QString>
#include <memory>
class ManuscriptEditor;
class QTextDocument;
class QIODevice;
namespace DocumentFormats {
enum class Format { Docx, Odt, Html, Markdown, Pdf, Rtf, Text, FixedText };
struct TextOptions { QString encoding = QStringLiteral("UTF-8"); int columns = 80; };
QString decode(const QByteArray &bytes, const QString &encoding, QString *error);
QString importFile(const QString &path, Format format, const TextOptions &options, QString *error);
bool exportFile(const QString &path, Format format, const TextOptions &options, ManuscriptEditor *editor, QString *error);
std::unique_ptr<QTextDocument> formattedDocument(ManuscriptEditor *editor);
QString fromRichText(const QTextDocument &document);
QString fromOffice(const QString &path, bool odt, QString *error);
bool writeOdt(QIODevice *target, const QTextDocument &document, QString *error);
bool writeDocx(QIODevice *target, const QTextDocument &document, QString *error);
QString fromRtf(const QByteArray &bytes, QString *error);
QString fromPdf(const QString &path, QString *error);
}
