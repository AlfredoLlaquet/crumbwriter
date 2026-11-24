#include "committedpunctuation.h"
#include "genericparagraph.h"
#include "manuscripttext.h"

#include <QtCore/QStringList>

#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>

namespace {
char32_t scalarAt(QTextDocument *doc, int pos)
{
    if (pos < 0 || pos >= doc->characterCount() - 1)
        return 0;
    const QChar ch = doc->characterAt(pos);
    const QChar next = doc->characterAt(pos + 1);
    return ch.isHighSurrogate() && next.isLowSurrogate()
        ? QChar::surrogateToUcs4(ch, next) : ch.unicode();
}

char32_t before(QTextDocument *doc, int pos)
{
    if (pos > 1 && doc->characterAt(pos - 1).isLowSurrogate()
                && doc->characterAt(pos - 2).isHighSurrogate())
        return scalarAt(doc, pos - 2);
    return scalarAt(doc, pos - 1);
}

bool isWord(char32_t ch)
{
    return QChar::isLetterOrNumber(ch) || QChar::isMark(ch);
}

void replace(QTextDocument *doc, int start, int end, QChar value)
{
    QTextCursor cursor(doc);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    ManuscriptText::insert(cursor, QString(value));
}

bool hasOpenQuote(QTextDocument *doc, int pos, bool single)
{
    const auto open = single ? u'‘' : u'“';
    const auto close = single ? u'’' : u'”';
    int depth = 0;
    for (int n = doc->findBlock(pos).position(); n < pos; ++n) {
        const QChar ch = doc->characterAt(n);
        if (ch == open)
            ++depth;
        else if (depth && ch == close &&
                 !(single && isWord(before(doc, n)) && isWord(scalarAt(doc, n + 1))))
            --depth;
    }
    return depth > 0;
}

bool closing(QTextDocument *doc, int pos, bool single)
{
    const auto left = before(doc, pos);
    const auto right = scalarAt(doc, pos + 1);
    const auto category = QChar::category(left);
    if (isWord(left) || (single && QChar::isDigit(right)) ||
        category == QChar::Punctuation_Close || category == QChar::Punctuation_FinalQuote)
        return true;
    if (right && !QChar::isSpace(right) && !QChar::isPunct(right))
        return false;
    return (left < 128 && QStringLiteral(".,!?:;").contains(QChar(ushort(left)))) ||
           hasOpenQuote(doc, pos, single);
}
}

void CommittedPunctuation::reset()
{
    length_ = 0;
    elisionAt_ = -1;
}

int CommittedPunctuation::resolve(QTextDocument *doc, bool generic)
{
    const int length = length_;
    length_ = 0;
    if (!length)
        return 0;
    for (int i = 0; i < length; ++i)
        if (doc->characterAt(start_ + i) != character_)
            return 0;

    QChar result;
    if (character_ == u'\'' || character_ == u'"') {
        const bool single = character_ == u'\'';
        const bool close = closing(doc, start_, single);
        result = single ? (close ? u'’' : u'‘') : (close ? u'”' : u'“');
        if (single && !close && QChar::isLetter(scalarAt(doc, start_ + 1))) {
            elisionAt_ = start_;
            elisionEnd_ = start_ + 1;
        }
    } else if (character_ == u'-' && length == 2) {
        if (generic && GenericParagraph::onlyHyphens(doc->findBlock(start_).text()))
            return 0;
        result = u'—';
    } else if (character_ == u'.' && length == 3) {
        result = u'…';
    }
    if (result.isNull())
        return 0;
    replace(doc, start_, start_ + length, result);
    return 1 - length;
}

void CommittedPunctuation::resolveElision(QTextDocument *doc, bool boundary)
{
    if (elisionAt_ < 0)
        return;
    static const QStringList words = {
        QStringLiteral("tis"), QStringLiteral("twas"), QStringLiteral("twere"),
        QStringLiteral("twill"), QStringLiteral("twould"), QStringLiteral("tween"),
        QStringLiteral("twixt"), QStringLiteral("em"), QStringLiteral("til"),
        QStringLiteral("cause"), QStringLiteral("bout"), QStringLiteral("cept"),
        QStringLiteral("round"), QStringLiteral("neath"), QStringLiteral("nuff"),
        QStringLiteral("n")
    };
    QString token;
    for (int pos = elisionAt_ + 1; token.size() < 9 && isWord(scalarAt(doc, pos)); ++pos)
        token += doc->characterAt(pos);
    if (!token.isEmpty() && doc->characterAt(elisionAt_) == u'‘') {
        for (const auto &word : words) {
            if (word.startsWith(token, Qt::CaseInsensitive)) {
                if (!boundary)
                    return;
                if (word.size() == token.size()) {
                    replace(doc, elisionAt_, elisionAt_ + 1, u'’');
                    break;
                }
            }
        }
    }
    elisionAt_ = -1;
}

void CommittedPunctuation::feed(QTextDocument *doc, int start, const QString &text, bool generic)
{
    int at = start;
    for (qsizetype i = 0; i < text.size();) {
        const QChar ch = text.at(i);
        const int units = ch.isHighSurrogate() && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate() ? 2 : 1;
        const auto scalar = units == 2 ? QChar::surrogateToUcs4(ch, text.at(i + 1)) : ch.unicode();
        if (elisionAt_ >= 0 && at != elisionEnd_)
            elisionAt_ = -1;
        if (length_ && at != start_ + length_)
            length_ = 0;
        if (length_ && ch == character_ &&
            ((ch == u'-' && length_ < 2) || (ch == u'.' && length_ < 3))) {
            ++length_;
        } else {
            at += resolve(doc, generic);
            if (elisionAt_ >= 0) {
                elisionEnd_ = at + units;
                resolveElision(doc, !isWord(scalar));
            }
            if (ch == u'\'' || ch == u'"' || ch == u'-' || ch == u'.') {
                start_ = at;
                character_ = ch;
                length_ = 1;
            }
        }
        i += units;
        at += units;
    }
}

void CommittedPunctuation::finish(QTextDocument *doc, bool generic)
{
    resolve(doc, generic);
    resolveElision(doc, true);
    reset();
}
