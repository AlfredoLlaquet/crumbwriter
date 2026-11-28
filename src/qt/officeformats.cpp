#include "documentformats.h"
#include <QBuffer>
#include <QDomDocument>
#include <QtEndian>
#include <cstring>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QXmlStreamWriter>
#include <archive.h>
#include <archive_entry.h>

namespace DocumentFormats {
namespace {
using Parts = QMap<QString, QByteArray>;
const QString wordNs = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");

Parts readParts(const QString &path, const QStringList &wanted, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { *error = input.errorString(); return {}; }
    std::unique_ptr<archive, decltype(&archive_read_free)> zip(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(zip.get());
    if (archive_read_open_fd(zip.get(), input.handle(), 65536) != ARCHIVE_OK) {
        *error = QObject::tr("Invalid Office archive."); return {};
    }
    Parts parts;
    archive_entry *entry;
    int status;
    qint64 total = 0;
    while ((status = archive_read_next_header(zip.get(), &entry)) == ARCHIVE_OK) {
        const auto name = QString::fromUtf8(archive_entry_pathname(entry));
        if (!wanted.contains(name)) continue;
        QByteArray data;
        char buffer[32768];
        la_ssize_t count;
        while ((count = archive_read_data(zip.get(), buffer, sizeof(buffer))) > 0) {
            total += count;
            if (total > 128LL * 1024 * 1024) { *error = QObject::tr("Office XML exceeds the 128 MiB import limit."); return {}; }
            data.append(buffer, count);
        }
        if (count < 0) { *error = QObject::tr("A compressed Office document part could not be read."); return {}; }
        parts.insert(name, data);
    }
    if (status != ARCHIVE_EOF) *error = QObject::tr("The Office archive is incomplete.");
    return parts;
}

// The ODF type detector in LibreOffice needs a conventional seekable ZIP.
// libarchive emits streaming data descriptors. Its central directory already
// contains the finalized checksums and sizes for building ordinary local headers.
bool finishOdtZip(QIODevice *output, const QByteArray &zip)
{
    constexpr int endSize = 22, centralSize = 46, localSize = 30;
    if (zip.size() < endSize) return false;
    auto end = zip.right(endSize);
    if (!end.startsWith("PK\005\006")) return false;
    const auto u32 = [](const char *p) { return qFromLittleEndian<quint32>(p); };
    const auto u16 = [](const char *p) { return qFromLittleEndian<quint16>(p); };
    const qint64 directoryAt = u32(end.constData() + 16);
    const qint64 directoryBytes = u32(end.constData() + 12);
    if (directoryAt + directoryBytes != zip.size() - endSize) return false;
    auto directory = zip.mid(directoryAt, directoryBytes);
    qsizetype pos = 0;
    while (pos < directory.size()) {
        if (directory.size() - pos < centralSize) return false;
        char *entry = directory.data() + pos;
        if (std::memcmp(entry, "PK\001\002", 4)) return false;
        const qint64 offset = u32(entry + 42), compressed = u32(entry + 20);
        if (offset + localSize > directoryAt) return false;
        const char *local = zip.constData() + offset;
        if (std::memcmp(local, "PK\003\004", 4)) return false;
        const auto headerSize = localSize + u16(local + 26) + u16(local + 28);
        if (offset + headerSize + compressed > directoryAt) return false;
        auto header = zip.mid(offset, headerSize);
        std::memcpy(header.data() + 14, entry + 16, 12);
        const auto flags = quint16(u16(entry + 8) & ~0x0008);
        qToLittleEndian(flags, header.data() + 6);
        qToLittleEndian(flags, entry + 8);
        qToLittleEndian(quint32(output->pos()), entry + 42);
        if (output->write(header) != header.size() ||
            output->write(local + headerSize, compressed) != compressed) return false;
        pos += centralSize + u16(entry + 28) + u16(entry + 30) + u16(entry + 32);
    }
    qToLittleEndian(quint32(output->pos()), end.data() + 16);
    return pos == directory.size() && output->write(directory) == directory.size() &&
           output->write(end) == end.size();
}

bool writeParts(QIODevice *output, const Parts &parts, QString *error)
{
    std::unique_ptr<archive, decltype(&archive_write_free)> zip(archive_write_new(), archive_write_free);
    archive_write_set_format_zip(zip.get());
    archive_write_set_bytes_per_block(zip.get(), 0);
    auto write = [](archive *, void *device, const void *data, size_t size) -> la_ssize_t {
        return static_cast<QIODevice *>(device)->write(static_cast<const char *>(data), size);
    };
    const bool odt = parts.contains("mimetype");
    QBuffer packed;
    if (odt) packed.open(QIODevice::WriteOnly);
    bool ok = archive_write_open(zip.get(), odt ? &packed : output, nullptr, write, nullptr) == ARCHIVE_OK;
    auto names = parts.keys();
    if (names.removeOne("mimetype")) names.prepend("mimetype");
    for (const auto &name : names) {
        if (!ok) break;
        const auto it = parts.constFind(name);
        if (name == "mimetype") archive_write_zip_set_compression_store(zip.get());
        else archive_write_zip_set_compression_deflate(zip.get());
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        archive_entry_set_pathname(entry.get(), it.key().toUtf8().constData());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), it.value().size());
        ok = archive_write_header(zip.get(), entry.get()) == ARCHIVE_OK &&
            archive_write_data(zip.get(), it.value().constData(), it.value().size()) == it.value().size();
    }
    ok = archive_write_close(zip.get()) == ARCHIVE_OK && ok;
    if (ok && odt) ok = finishOdtZip(output, packed.data());
    if (!ok) *error = QObject::tr("Could not write the Office archive.");
    return ok;
}

QString local(const QDomElement &e) { return e.tagName().section(u':', -1); }
QString attr(const QDomElement &e, const QString &key)
{
    const auto attributes = e.attributes();
    for (int i = 0; i < attributes.size(); ++i) {
        const auto entry = attributes.item(i);
        if (entry.nodeName().section(u':', -1) == key) return entry.nodeValue();
    }
    return {};
}
int wordHeadingLevel(const QDomElement &outline)
{
    bool ok;
    const int level = attr(outline, "val").toInt(&ok);
    return ok && level >= 0 && level < 9 ? level + 1 : 0;
}

QDomElement child(const QDomElement &parent, const QString &name)
{
    for (auto e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
        if (local(e) == name) return e;
    return {};
}
void visit(const QDomElement &node, const std::function<void(const QDomElement &)> &fn)
{
    if (node.isNull()) return;
    fn(node);
    for (auto e = node.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) visit(e, fn);
}

QDomDocument parse(const QByteArray &data, QString *error)
{
    QDomDocument doc;
    if (!data.isEmpty() && !doc.setContent(data)) *error = QObject::tr("Invalid XML in the Office document.");
    return doc;
}

struct OfficeStyles {
    QHash<QString, int> heading;
    QSet<QString> italics, bulletLists;
    QHash<QString, QString> listDefinitions;
    QHash<QString, int> counters;
};

void wordText(const QDomElement &p, QTextCursor &cursor, bool italic = false)
{
    const auto tag = local(p);
    if (tag == "del" || tag == "pPr" || tag == "drawing" || tag == "instrText") return;
    if (tag == "r") {
        const auto i = child(child(p, "rPr"), "i");
        if (!i.isNull()) italic = attr(i, "val") != "0" && attr(i, "val") != "false";
    }
    QTextCharFormat fmt;
    fmt.setFontItalic(italic);
    if (tag == "t") { cursor.insertText(p.text(), fmt); return; }
    if (tag == "tab" || tag == "br") { cursor.insertText(" ", fmt); return; }
    for (auto e = p.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) wordText(e, cursor, italic);
}

void odtText(const QDomElement &parent, QTextCursor &cursor, const OfficeStyles &styles, bool italic = false)
{
    italic |= styles.italics.contains(attr(parent, "style-name"));
    QTextCharFormat fmt;
    fmt.setFontItalic(italic);
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isText()) { cursor.insertText(n.nodeValue(), fmt); continue; }
        const auto e = n.toElement();
        const auto tag = local(e);
        if (tag == "s") cursor.insertText(QString(qBound(1, attr(e,"c").toInt(), 65536), u' '), fmt);
        else if (tag == "tab" || tag == "line-break") cursor.insertText(" ", fmt);
        else if (tag != "annotation" && tag != "note" && tag != "frame") odtText(e, cursor, styles, italic);
    }
}

void paragraphs(const QDomElement &node, bool odt, OfficeStyles &styles, QTextCursor &cursor, bool &first,
                const QString &odtPrefix = {})
{
    const auto tag = local(node);
    if (tag == "list" && odt) {
        int number = 0;
        const QString style = attr(node, "style-name");
        for (auto item = node.firstChildElement(); !item.isNull(); item = item.nextSiblingElement()) {
            if (local(item) != "list-item") continue;
            const QString prefix = styles.bulletLists.contains(style) ? QStringLiteral("* ") : QString::number(++number) + ". ";
            bool firstChild = true;
            for (auto p = item.firstChildElement(); !p.isNull(); p = p.nextSiblingElement()) {
                paragraphs(p, odt, styles, cursor, first, firstChild ? prefix : QString());
                firstChild = false;
            }
        }
        return;
    }
    if (tag != "p" && !(odt && tag == "h")) {
        if (tag == "del" || tag == "annotation" || tag == "note") return;
        for (auto e = node.firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
            paragraphs(e, odt, styles, cursor, first);
        return;
    }
    if (!first) cursor.insertBlock();
    first = false;
    QTextBlockFormat fmt;
    QString prefix = odtPrefix;
    if (odt) {
        int level = attr(node, "outline-level").toInt();
        if (!level) level = styles.heading.value(attr(node, "style-name"));
        fmt.setHeadingLevel(qBound(0, level, 6));
    } else {
        const auto properties = child(node, "pPr");
        const QString style = attr(child(properties, "pStyle"), "val");
        int heading = styles.heading.value(style);
        if (!styles.heading.contains(style) && style.compare("Title", Qt::CaseInsensitive) == 0) heading = 1;
        static const QRegularExpression levelPattern("^Heading\\s*([1-9])$", QRegularExpression::CaseInsensitiveOption);
        const auto match = levelPattern.match(style);
        if (!styles.heading.contains(style) && match.hasMatch()) heading = match.captured(1).toInt();
        const auto outline = child(properties, "outlineLvl");
        if (!outline.isNull()) heading = wordHeadingLevel(outline);
        fmt.setHeadingLevel(qBound(0, heading, 6));
        const auto num = child(properties, "numPr");
        if (!num.isNull()) {
            const QString id = attr(child(num, "numId"), "val"), level = attr(child(num, "ilvl"), "val");
            if (id != "0") {
                const QString key = id + "/" + level;
                prefix = styles.bulletLists.contains(styles.listDefinitions.value(id) + "/" + level)
                    ? QStringLiteral("* ") : QString::number(++styles.counters[key]) + ". ";
            }
        }
    }
    cursor.setBlockFormat(fmt);
    if (!prefix.isEmpty()) cursor.insertText(prefix, QTextCharFormat());
    if (odt) odtText(node, cursor, styles);
    else wordText(node, cursor);
}

void property(QXmlStreamWriter &xml, const QString &tag, const QString &value)
{
    xml.writeEmptyElement("w:" + tag);
    xml.writeAttribute("w:val", value);
}
}

QString fromOffice(const QString &path, bool odt, QString *error)
{
    const QString content = odt ? "content.xml" : "word/document.xml";
    const auto parts = readParts(path, {content, "styles.xml", "word/styles.xml", "word/numbering.xml"}, error);
    if (!error->isEmpty()) return {};
    if (!parts.contains(content)) { *error = QObject::tr("This archive does not contain the selected Office document format."); return {}; }
    const auto doc = parse(parts.value(content), error);
    const auto stylesDoc = parse(parts.value(odt ? "styles.xml" : "word/styles.xml"), error);
    const auto numbers = parse(parts.value("word/numbering.xml"), error);
    if (!error->isEmpty()) return {};
    OfficeStyles styles;
    auto style = [&](const QDomElement &e) {
        const auto tag = local(e);
        if (tag == "style") {
            if (odt) {
                const QString name = attr(e, "name");
                if (attr(child(e, "text-properties"), "font-style") == "italic") styles.italics.insert(name);
                const auto level = attr(e, "default-outline-level").toInt();
                if (level) styles.heading[name] = level;
            } else {
                const auto outline = child(child(e, "pPr"), "outlineLvl");
                if (!outline.isNull()) styles.heading[attr(e, "styleId")] = wordHeadingLevel(outline);
            }
        } else if (tag == "list-style" && !child(e, "list-level-style-bullet").isNull()) styles.bulletLists.insert(attr(e, "name"));
        else if (tag == "num") styles.listDefinitions[attr(e, "numId")] = attr(child(e, "abstractNumId"), "val");
        else if (tag == "abstractNum") {
            for (auto level = e.firstChildElement(); !level.isNull(); level = level.nextSiblingElement())
                if (attr(child(level, "numFmt"), "val") == "bullet") styles.bulletLists.insert(attr(e, "abstractNumId") + "/" + attr(level, "ilvl"));
        }
    };
    visit(stylesDoc.documentElement(), style);
    visit(numbers.documentElement(), style);
    if (odt) visit(child(doc.documentElement(), "automatic-styles"), style);
    const auto body = child(doc.documentElement(), "body");
    if (body.isNull()) { *error = QObject::tr("The document body is missing."); return {}; }
    QTextDocument result;
    QTextCursor cursor(&result);
    bool first = true;
    paragraphs(body, odt, styles, cursor, first);
    return fromRichText(result);
}

bool writeDocx(QIODevice *target, const QTextDocument &document, QString *error)
{
    QByteArray content;
    QXmlStreamWriter xml(&content);
    xml.writeStartDocument();
    xml.writeStartElement("w:document");
    xml.writeNamespace(wordNs, "w");
    xml.writeStartElement("w:body");
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        xml.writeStartElement("w:p");
        const int level = block.blockFormat().headingLevel();
        if (level) {
            xml.writeStartElement("w:pPr");
            property(xml, "pStyle", "Heading" + QString::number(level));
            xml.writeEmptyElement("w:spacing");
            xml.writeAttribute("w:before", QString::number(qRound(block.blockFormat().topMargin() * 15)));
            xml.writeEndElement();
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto run = it.fragment();
            if (!run.isValid()) continue;
            const auto font = run.charFormat().font().resolve(document.defaultFont());
            xml.writeStartElement("w:r");
            xml.writeStartElement("w:rPr");
            xml.writeEmptyElement("w:rFonts");
            for (const auto *attribute : {"w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"}) xml.writeAttribute(attribute, font.family());
            if (font.bold()) xml.writeEmptyElement("w:b");
            if (font.italic()) xml.writeEmptyElement("w:i");
            property(xml, "sz", QString::number(qRound(font.pointSizeF() * 2)));
            xml.writeEndElement();
            xml.writeStartElement("w:t");
            xml.writeAttribute("xml:space","preserve");
            xml.writeCharacters(run.text());
            xml.writeEndElement(); xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndDocument();
    QByteArray styleBytes;
    QXmlStreamWriter styles(&styleBytes);
    styles.writeStartElement("w:styles");
    styles.writeNamespace(wordNs, "w");
    styles.writeStartElement("w:style");
    styles.writeAttribute("w:type", "paragraph");
    styles.writeAttribute("w:default", "1");
    styles.writeAttribute("w:styleId", "Normal");
    property(styles, "name", "Normal");
    styles.writeStartElement("w:rPr");
    styles.writeEmptyElement("w:rFonts");
    styles.writeAttribute("w:ascii", document.defaultFont().family());
    styles.writeAttribute("w:hAnsi", document.defaultFont().family());
    property(styles, "sz", QString::number(qRound(document.defaultFont().pointSizeF() * 2)));
    styles.writeEndElement();
    styles.writeEndElement();
    for (int i = 1; i <= 4; ++i) {
        const QString id = "Heading" + QString::number(i);
        styles.writeStartElement("w:style");
        styles.writeAttribute("w:type","paragraph"); styles.writeAttribute("w:styleId",id);
        property(styles,"name",id);
        styles.writeStartElement("w:pPr");
        property(styles, "outlineLvl", QString::number(i - 1));
        styles.writeEndElement();
        styles.writeEndElement();
    }
    styles.writeEndDocument();
    const QByteArray types = R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/></Types>)";
    const QByteArray relStart = R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/)";
    return writeParts(target, {{"[Content_Types].xml", types}, {"word/document.xml", content}, {"word/styles.xml", styleBytes},
        {"_rels/.rels", relStart + R"(officeDocument" Target="word/document.xml"/></Relationships>)"},
        {"word/_rels/document.xml.rels", relStart + R"(styles" Target="styles.xml"/></Relationships>)"}}, error);
}
}

namespace DocumentFormats {
bool writeOdt(QIODevice *output, const QTextDocument &doc, QString *error)
{
    const QByteArray mime = "application/vnd.oasis.opendocument.text";
    QByteArray content;
    QXmlStreamWriter xml(&content);
    xml.writeStartDocument();
    xml.writeStartElement("office:document-content");
    const QString ns = "urn:oasis:names:tc:opendocument:xmlns:";
    for (const auto *prefix : {"office", "style", "text"})
        xml.writeNamespace(ns + prefix + ":1.0", prefix);
    xml.writeNamespace(ns + "xsl-fo-compatible:1.0", "fo");
    xml.writeAttribute("office:version", "1.2");
    xml.writeStartElement("office:font-face-decls");
    xml.writeEmptyElement("style:font-face");
    xml.writeAttribute("style:name", "WritingFont");
    xml.writeNamespace(ns + "svg-compatible:1.0", "svg");
    xml.writeAttribute("svg:font-family", doc.defaultFont().family());
    xml.writeEndElement();
    xml.writeStartElement("office:automatic-styles");
    QMap<int, QString> styles;
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto run = it.fragment();
            if (!run.isValid() || styles.contains(run.charFormatIndex())) continue;
            const auto name = "T" + QString::number(styles.size() + 1);
            styles.insert(run.charFormatIndex(), name);
            const auto font = run.charFormat().font().resolve(doc.defaultFont());
            xml.writeStartElement("style:style");
            xml.writeAttribute("style:name", name);
            xml.writeAttribute("style:family", "text");
            xml.writeEmptyElement("style:text-properties");
            xml.writeAttribute("style:font-name", "WritingFont");
            xml.writeAttribute("fo:font-size", QString::number(font.pointSizeF()) + "pt");
            xml.writeAttribute("fo:font-weight", font.bold() ? "bold" : "normal");
            xml.writeAttribute("fo:font-style", font.italic() ? "italic" : "normal");
            xml.writeEndElement();
        }
    }
    xml.writeEndElement();
    xml.writeStartElement("office:body");
    xml.writeStartElement("office:text");
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        xml.writeStartElement(level ? "text:h" : "text:p");
        if (level) xml.writeAttribute("text:outline-level", QString::number(level));
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto run = it.fragment();
            if (!run.isValid()) continue;
            xml.writeStartElement("text:span");
            xml.writeAttribute("text:style-name", styles.value(run.charFormatIndex()));
            const auto t = run.text();
            for (qsizetype start = 0; start < t.size();) {
                const bool whitespace = t.at(start) == u' ';
                auto end = start + 1;
                while (end < t.size() && (t.at(end) == u' ') == whitespace) ++end;
                if (whitespace) {
                    xml.writeEmptyElement("text:s");
                    if (end > start + 1) xml.writeAttribute("text:c", QString::number(end - start));
                } else xml.writeCharacters(t.mid(start, end - start));
                start = end;
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndDocument();
    const QByteArray manifest = R"(<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2"><manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/><manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/></manifest:manifest>)";
    return writeParts(output, {{"mimetype", mime}, {"content.xml", content}, {"META-INF/manifest.xml", manifest}}, error);
}
}
