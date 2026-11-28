#include "documentformats.h"
#include "genericparagraph.h"
#include "guillemetmarkup.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"
#include <QFontInfo>
#include <QTextList>

namespace DocumentFormats {
std::unique_ptr<QTextDocument> formattedDocument(ManuscriptEditor *editor)
{
    auto result = std::make_unique<QTextDocument>();
    auto font = editor->font();
    font.setFamily(QFontInfo(font).family());
    result->setDefaultFont(font);
    QTextCursor cursor(result.get());
    bool first = true;
    for (auto block = editor->document()->begin(); block.isValid(); block = block.next()) {
        const auto &rec = editor->model().record(block.blockNumber());
        const bool generic = editor->model().isGeneric();
        if (generic && rec.role == ParagraphRole::TitleMarker) continue;
        if (!first) cursor.insertBlock();
        first = false;
        QTextBlockFormat paragraph;
        QTextCharFormat style;
        style.setFont(font);
        const int level = generic ? rec.headingLevel : 0;
        if (level) {
            const qreal scale = rec.role == ParagraphRole::DocumentTitle
                ? GenericParagraph::mainTitleScale : GenericParagraph::titleScales[level - 1];
            style.setFontPointSize(font.pointSizeF() * scale);
            style.setFontWeight(QFont::Bold);
            paragraph.setHeadingLevel(level);
            paragraph.setTopMargin(font.pointSizeF() * .45);
        }
        cursor.setBlockFormat(paragraph);
        cursor.setBlockCharFormat(style);
        const auto text = ManuscriptText::blockText(block);
        if (generic && !editor->literalGuillemets()) {
            int start = 0;
            for (const auto span : GuillemetMarkup::pairs(text)) {
                cursor.insertText(text.mid(start, span.open - start), style);
                auto italic = style;
                italic.setFontItalic(true);
                cursor.insertText(text.mid(span.open + 1, span.close - span.open - 1), italic);
                start = span.close + 1;
            }
            cursor.insertText(text.mid(start), style);
        } else if (!generic && rec.valid) {
            auto header = style, title = style;
            header.setFontItalic(true);
            title.setFontWeight(QFont::Bold);
            cursor.insertText(text.left(rec.headerStart), style);
            cursor.insertText(text.mid(rec.headerStart, rec.headerEnd - rec.headerStart), header);
            cursor.insertText(text.mid(rec.headerEnd, rec.titleStart - rec.headerEnd), style);
            cursor.insertText(text.mid(rec.titleStart, rec.titleEnd - rec.titleStart), title);
            cursor.insertText(text.mid(rec.titleEnd), style);
        } else cursor.insertText(text, style);
    }
    return result;
}

QString fromRichText(const QTextDocument &document)
{
    QStringList lines;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        QString line;
        bool italic = false;
        if (const auto *list = block.textList()) {
            const auto kind = list->format().style();
            line = kind == QTextListFormat::ListDisc || kind == QTextListFormat::ListCircle ||
                   kind == QTextListFormat::ListSquare ? QStringLiteral("* ") : list->itemText(block) + u' ';
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto part = it.fragment();
            if (!part.isValid()) continue;
            const bool next = part.charFormat().fontItalic();
            if (italic != next) line += next ? u'«' : u'»';
            line += part.text();
            italic = next;
        }
        if (italic) line += u'»';
        const int level = block.blockFormat().headingLevel();
        if (level > 1 && lines.isEmpty()) lines << QString();
        lines << line;
        if (level && !line.isEmpty())
            lines << QString(GenericParagraph::markerLength(line), QStringLiteral("=-~.").at(qBound(0, level - 1, 3)));
    }
    return lines.join(u'\n');
}
}
