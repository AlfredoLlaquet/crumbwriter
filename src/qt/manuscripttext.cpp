#include "manuscripttext.h"

#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QTextFormat>

namespace {
constexpr int originalCodeUnit = QTextFormat::UserProperty + 1;
constexpr auto hasLiterals = "crumbwriterLiteralCharacters";

bool reserved(QChar ch)
{
    return ch == QChar::ParagraphSeparator || ch == QChar(0xfdd0) || ch == QChar(0xfdd1);
}

qsizetype nextReserved(const QString &text, qsizetype from = 0)
{
    for (qsizetype i = from; i < text.size(); ++i)
        if (reserved(text.at(i)))
            return i;
    return text.size();
}

QString restore(const QTextDocument *doc, int start, QString text)
{
    if (!doc->property(hasLiterals).toBool())
        return text;
    // A cursor at the end of an encoded character sees that character's own
    // format. Native Qt undo and range edits track these formats automatically.
    QTextCursor cursor(doc->begin());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != QChar::LineSeparator && text.at(i) != QChar::ReplacementCharacter)
            continue;
        cursor.setPosition(start + int(i) + 1);
        const auto original = cursor.charFormat().intProperty(originalCodeUnit);
        if (original)
            text[i] = QChar(static_cast<ushort>(original));
    }
    return text;
}
}

void ManuscriptText::load(QTextDocument *doc, const QString &text)
{
    doc->setProperty(hasLiterals, false);
    if (nextReserved(text) == text.size()) {
        doc->setPlainText(text);
        return;
    }
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    insert(cursor, text);
    cursor.endEditBlock();
    doc->setUndoRedoEnabled(undo);
}

void ManuscriptText::insert(QTextCursor &cursor, const QString &text)
{
    QTextDocument *doc = cursor.document();
    const qsizetype first = nextReserved(text);
    if (first == text.size() && !doc->property(hasLiterals).toBool()) {
        cursor.insertText(text);
        return;
    }
    QTextCharFormat normal = cursor.charFormat();
    normal.clearProperty(originalCodeUnit);
    if (first == text.size()) {
        cursor.insertText(text, normal);
        return;
    }
    doc->setProperty(hasLiterals, true);
    qsizetype pos = 0;
    for (qsizetype i = first; i < text.size(); i = nextReserved(text, pos)) {
        if (i > pos)
            cursor.insertText(text.mid(pos, i-pos), normal);
        QTextCharFormat literal = normal;
        const auto value = text.at(i);
        literal.setProperty(originalCodeUnit, int(value.unicode()));
        const QChar visible = value == QChar::ParagraphSeparator
                ? QChar::LineSeparator : QChar::ReplacementCharacter;
        cursor.insertText(QString(visible), literal);
        pos = i + 1;
    }
    if (pos < text.size())
        cursor.insertText(text.mid(pos), normal);
}

QString ManuscriptText::documentText(const QTextDocument *doc)
{
    // Qt's toPlainText() also replaces NBSP. Convert only physical line ends.
    QString text = doc->toRawText();
    text.replace(QChar::ParagraphSeparator, u'\n');
    return restore(doc, 0, text);
}

QString ManuscriptText::blockText(const QTextBlock &block)
{
    if (!block.isValid())
        return {};
    return restore(block.document(), block.position(), block.text());
}

QString ManuscriptText::selectedText(const QTextCursor &cursor)
{
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, u'\n');
    return restore(cursor.document(), cursor.selectionStart(), text);
}
