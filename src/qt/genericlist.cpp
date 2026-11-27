#include "genericlist.h"
#include "manuscripttext.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFormat>
#include <algorithm>

namespace {
// This property belongs to the first character only. Native undo/redo moves
// it with the prefix, and plain-text save, copy and paste omit it.
constexpr int generatedPrefix = QTextFormat::UserProperty + 2;

struct Item {
    QString indent;
    QString opening;
    QString counter;
    QString closing;
    bool bullet = false;

    QString prefix(const QString &next) const { return indent + opening + next + closing + u' '; }
};

Item itemAt(const QTextBlock &block)
{
    Item item;
    if (!block.isValid()) return item;
    const QString text = block.text();
    qsizetype first = 0;
    while (first < text.size() && text[first] == u' ') ++first;
    const auto space = text.indexOf(u' ', first);
    if (space < 0 || QStringView(text).mid(space + 1).trimmed().isEmpty()) return item;
    item.indent = text.left(first);
    QString token = text.mid(first, space - first);
    if (token == "-" || token == "*" || token == "(*)") {
        item.bullet = true;
    } else {
        if (token.startsWith(u'(')) {
            if (!token.endsWith(u')')) return {};
            item.opening = QStringLiteral("(");
            token.remove(0, 1);
        }
        if (token.endsWith(u'.') || token.endsWith(u')')) {
            item.closing = token.right(1);
            token.chop(1);
        }
    }
    item.counter = token;
    return item;
}

QString nextDecimal(QString counter)
{
    // Increment only the final component. Text arithmetic preserves leading
    // zeros in outlines and does not impose an integer limit on manuscripts.
    bool needsDigit = true;
    qsizetype component = 0;
    for (qsizetype i = 0; i < counter.size(); ++i) {
        if (counter[i] >= u'0' && counter[i] <= u'9') {
            needsDigit = false;
        } else if (counter[i] == u'.' && !needsDigit) {
            component = i + 1;
            needsDigit = true;
        } else {
            return {};
        }
    }
    if (needsDigit) return {};
    for (qsizetype i = counter.size(); i-- > component;) {
        if (counter[i] != u'9') {
            counter[i] = QChar(counter[i].unicode() + 1);
            return counter;
        }
        counter[i] = u'0';
    }
    return counter.insert(component, u'1');
}

bool letters(const QString &code, bool lower)
{
    return !code.isEmpty() && std::all_of(code.cbegin(), code.cend(), [lower](QChar ch) {
        return lower ? ch >= u'a' && ch <= u'z' : ch >= u'A' && ch <= u'Z';
    });
}

QString nextLetters(QString code, bool lower)
{
    const QChar first = lower ? u'a' : u'A';
    const QChar last = lower ? u'z' : u'Z';
    for (qsizetype i = code.size(); i-- > 0;) {
        if (code[i] != last) {
            code[i] = QChar(code[i].unicode() + 1);
            return code;
        }
        code[i] = first;
    }
    return code.prepend(first);
}

struct RomanPart { int value; const char *digits; };
constexpr RomanPart romanParts[] = {
    {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
    {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
};

QString romanNumber(int value, bool lower)
{
    if (value < 1 || value > 3999) return {};
    QString result;
    for (const auto &part : romanParts) {
        while (value >= part.value) {
            value -= part.value;
            result += QLatin1String(part.digits);
        }
    }
    return lower ? result.toLower() : result;
}

int romanValue(const QString &code)
{
    if (code.isEmpty() || code.size() > 15) return 0;
    const QString upper = code.toUpper();
    QStringView rest(upper);
    int value = 0;
    for (const auto &part : romanParts) {
        const QLatin1String digits(part.digits);
        while (rest.startsWith(digits)) {
            value += part.value;
            rest = rest.mid(digits.size());
        }
    }
    return rest.isEmpty() && romanNumber(value, false) == upper ? value : 0;
}
}

QString GenericList::nextPrefix(const QTextBlock &block, bool explicitRequest)
{
    const Item item = itemAt(block);
    const QString &code = item.counter;
    if (code.isEmpty()) return {};
    if (item.bullet) return item.prefix(code);

    const bool bare = item.opening.isEmpty() && item.closing.isEmpty();
    if (code.front().isDigit()) {
        if (bare && !code.contains(u'.')) return {};
        const QString next = nextDecimal(code);
        return next.isEmpty() ? QString() : item.prefix(next);
    }

    const bool lower = code.front().isLower();
    if (!letters(code, lower)) return {};
    const Item previous = itemAt(block.previous());
    const bool sameStyle = item.indent == previous.indent && item.opening == previous.opening &&
                           item.closing == previous.closing && letters(previous.counter, lower);
    const bool followsLetter = sameStyle && nextLetters(previous.counter, lower) == code;
    const int roman = romanValue(code);
    const int previousRoman = sameStyle ? romanValue(previous.counter) : 0;
    const bool followsRoman = previousRoman && roman == previousRoman + 1;
    if (bare && !explicitRequest && !followsLetter && !followsRoman) return {};

    if (!followsLetter && roman) {
        const QString next = romanNumber(roman + 1, lower);
        return next.isEmpty() ? QString() : item.prefix(next);
    }
    if (code.size() == 1 || followsLetter ||
        (explicitRequest && !bare && code.size() == 2))
        return item.prefix(nextLetters(code, lower));
    return {};
}

void GenericList::insertPrefix(QTextCursor &cursor, const QString &prefix)
{
    if (prefix.isEmpty()) return;
    const int start = cursor.position();
    ManuscriptText::insert(cursor, prefix);
    auto first = cursor;
    first.setPosition(start);
    first.setPosition(start + 1, QTextCursor::KeepAnchor);
    QTextCharFormat provenance;
    provenance.setProperty(generatedPrefix, prefix);
    first.mergeCharFormat(provenance);
}

bool GenericList::selectEmptyGeneratedPrefix(QTextCursor &cursor)
{
    if (cursor.hasSelection() || !cursor.atBlockEnd() || cursor.atBlockStart()) return false;
    QTextCursor first(cursor.block());
    first.movePosition(QTextCursor::NextCharacter);
    const QString prefix = first.charFormat().stringProperty(generatedPrefix);
    if (prefix.isEmpty() || cursor.block().text() != prefix) return false;
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    return true;
}

namespace {
QString generatedAtStart(const QTextBlock &block)
{
    QTextCursor first(block);
    first.movePosition(QTextCursor::NextCharacter);
    return first.charFormat().stringProperty(generatedPrefix);
}

// Bare letters are ambiguous in prose. Only accept one when confirming the
// proposed token. A punctuated pasted number is allowed to override numbering.
bool explicitPrefix(const QString &text, const QString &proposal)
{
    static const QRegularExpression marked(QStringLiteral(
        R"(^ *(?:[-*]|\(\*\)|\([A-Za-z0-9.]+\)|[0-9]+(?:\.[0-9]+)*[.)]|[0-9]+(?:\.[0-9]+)+|[A-Za-z]+[.)]) )"));
    return text.startsWith(proposal) || marked.match(text).hasMatch();
}
}

void GenericList::replaceGeneratedPrefixForPaste(QTextCursor &cursor, const QString &text)
{
    auto selection = cursor;
    if (selectEmptyGeneratedPrefix(selection) && explicitPrefix(text, cursor.block().text()))
        cursor = selection;
}

void GenericList::resolveTypedPrefix(QTextCursor &cursor)
{
    if (!cursor.atBlockEnd()) return;
    const auto block = cursor.block();
    const QString proposed = generatedAtStart(block);
    if (proposed.isEmpty() || !block.text().startsWith(proposed)) return;
    const QString typed = block.text().mid(proposed.size());
    if (typed.isEmpty()) return;
    const auto token = proposed.trimmed();
    if (typed == token && !token.back().isLetterOrNumber()) {
        cursor.setPosition(block.position() + proposed.size(), QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else if (explicitPrefix(typed, proposed)) {
        QTextCursor erase(block);
        erase.setPosition(block.position() + proposed.size(), QTextCursor::KeepAnchor);
        erase.removeSelectedText();
    }
}
