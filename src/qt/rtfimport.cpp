#include "documentformats.h"
#include <QSet>
#include <QTextCursor>
#include <QTextDocument>
#include <cctype>

QString DocumentFormats::fromRtf(const QByteArray &data, QString *error)
{
    if (!data.startsWith("{\\rtf")) { *error = QObject::tr("The file is not an RTF document."); return {}; }
    struct State { bool skip = false, italic = false; int fallback = 1, codepage = 1252; };
    State current;
    QList<State> stack;
    QTextDocument document;
    QTextCursor cursor(&document);
    QByteArray text;
    int remaining = 0;
    auto insert = [&](const QString &s) {
        if (current.skip) return;
        QTextCharFormat fmt;
        fmt.setFontItalic(current.italic);
        cursor.insertText(s, fmt);
    };
    auto flush = [&] {
        if (!text.isEmpty() && !current.skip) insert(decode(text, "CP" + QString::number(current.codepage), error));
        text.clear();
    };
    auto byte = [&](char value) { if (remaining) --remaining; else if (!current.skip) text += value; };
    static const QSet<QByteArray> destinations = {"fonttbl","colortbl","stylesheet","info","pict","object","header","footer",
        "headerl","headerr","footerl","footerr","filetbl","listtable","listoverridetable","themedata","fldinst","datastore"};
    for (qsizetype i = 0; i < data.size() && error->isEmpty();) {
        const char c = data.at(i++);
        if (c == '{') { flush(); stack.append(current); continue; }
        if (c == '}') {
            flush();
            if (stack.isEmpty()) { *error = QObject::tr("Unbalanced RTF group."); break; }
            current = stack.takeLast(); continue;
        }
        if (c == '\r' || c == '\n') continue;
        if (c != '\\') { byte(c); continue; }
        if (i >= data.size()) break;
        const char control = data.at(i++);
        if (control == '\'' && i + 2 <= data.size()) {
            bool ok;
            const int ch = data.mid(i,2).toInt(&ok,16);
            if (ok) byte(char(ch)); else *error = QObject::tr("Invalid RTF hexadecimal escape.");
            i += 2; continue;
        }
        if (control == '\\' || control == '{' || control == '}') { byte(control); continue; }
        flush();
        if (!std::isalpha(static_cast<unsigned char>(control))) {
            if (control == '*') current.skip = true;
            else if (remaining) --remaining;
            else if (control == '~') insert(QString(QChar(0xa0)));
            else if (control == '_') insert(QString(QChar(0x2011)));
            continue;
        }
        const auto start = i-1;
        while (i < data.size() && std::isalpha(static_cast<unsigned char>(data[i]))) ++i;
        const auto word = data.mid(start, i-start);
        const auto numberStart = i;
        if (i < data.size() && data[i] == '-') ++i;
        while (i < data.size() && std::isdigit(static_cast<unsigned char>(data[i]))) ++i;
        const int value = i == numberStart ? 1 : data.mid(numberStart,i-numberStart).toInt();
        if (i < data.size() && data[i] == ' ') ++i;
        if (destinations.contains(word)) { current.skip = true; continue; }
        if (word == "bin") { i += qBound(qsizetype(0), qsizetype(value), data.size()-i); continue; }
        if (current.skip) continue;
        if (word == "i") current.italic = value != 0;
        else if (word == "plain") current.italic = false;
        else if (word == "ansicpg") current.codepage = value;
        else if (word == "uc") current.fallback = qBound(0,value,32);
        else if (word == "u") { insert(QString(QChar(quint16(value)))); remaining = current.fallback; }
        else if (word == "par" || word == "line") cursor.insertBlock();
        else if (word == "tab") insert(" ");
        else if (word == "emdash") insert(QString(QChar(0x2014)));
        else if (word == "endash") insert(QString(QChar(0x2013)));
        else if (word == "bullet") insert("*");
        else if (word == "lquote" || word == "rquote") insert(QString(QChar(word == "lquote" ? 0x2018 : 0x2019)));
        else if (word == "ldblquote" || word == "rdblquote") insert(QString(QChar(word == "ldblquote" ? 0x201c : 0x201d)));
        else if (word == "outlinelevel" || word == "pard") {
            auto fmt = cursor.blockFormat();
            fmt.setHeadingLevel(word == "pard" || value > 4 ? 0 : value+1);
            cursor.setBlockFormat(fmt);
        }
    }
    flush();
    if (!stack.isEmpty() && error->isEmpty()) *error = QObject::tr("The RTF document ends inside a group.");
    return error->isEmpty() ? fromRichText(document) : QString();
}
