#include "symbolcatalog.h"

#include <QFile>
#include <QHash>
#include <algorithm>

namespace {
QString fold(const QString &text)
{
    QString result;
    for (QChar ch : text.normalized(QString::NormalizationForm_D).toCaseFolded()) {
        if (ch.isLetterOrNumber())
            result += ch;
        else if (ch.category() != QChar::Mark_NonSpacing && !result.isEmpty() && result.back() != u' ')
            result += u' ';
    }
    if (result.endsWith(u' ')) result.chop(1);
    return result;
}

QString codeQuery(const QString &query)
{
    QString digits = query.toUpper();
    const bool prefixed = digits.startsWith("U+") || digits.startsWith("0X");
    if (prefixed) digits.remove(0, 2);
    if (digits.isEmpty() || digits.size() > 6 || (!prefixed && digits.size() < 4)) return {};
    bool includesDigit = false;
    for (QChar ch : digits) {
        if (ch >= u'0' && ch <= u'9') includesDigit = true;
        else if (ch < u'A' || ch > u'F') return {};
    }
    // Words such as "face" should still search the English descriptions.
    return prefixed || includesDigit ? digits : QString();
}
}

QString WriterSymbol::sample() const
{
    return showSpacing ? QStringLiteral("│") + text + QStringLiteral("│") : text;
}

const SymbolCatalog &SymbolCatalog::instance()
{
    static const SymbolCatalog catalog;
    return catalog;
}

SymbolCatalog::SymbolCatalog()
{
    QFile file(":/symbols/catalog.tsv");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString theme, themeWords;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        const auto fields = line.split(u'\t');
        if (line.startsWith(u'@')) {
            theme = fields.first().mid(1);
            themeWords = fields.value(1);
            themes_.append(theme);
            continue;
        }
        bool ok;
        const char32_t code = fields.first().toUInt(&ok, 16);
        if (!ok || code > QChar::LastValidCodePoint || QChar::isSurrogate(code) || fields.size() < 2 || theme.isEmpty()) continue;
        WriterSymbol entry;
        entry.text = QString::fromUcs4(&code, 1);
        entry.code = "U+" + fields.first();
        entry.officialName = fields.at(1);
        entry.name = fields.value(2);
        if (entry.name.isEmpty()) {
            entry.name = entry.officialName.toLower();
            entry.name[0] = entry.name.at(0).toUpper();
        }
        entry.theme = theme;
        entry.showSpacing = theme == QStringLiteral("Spacing");
        entry.aliases = fields.value(3).split(u'|', Qt::SkipEmptyParts);
        entry.foldedName = fold(entry.name);
        entry.foldedOfficialName = fold(entry.officialName);
        entry.keywords = fold(entry.name + u' ' + entry.officialName + u' ' + theme + u' ' +
                              themeWords + u' ' + entry.aliases.join(u' '));
        if (entry.foldedOfficialName.contains("black")) entry.keywords += " filled solid";
        if (entry.foldedOfficialName.contains("white")) entry.keywords += " outline outlined hollow";
        entries_.append(std::move(entry));
    }
}

QList<int> SymbolCatalog::search(const QString &query, const QString &theme, const QStringList *scope) const
{
    const QString raw = query.trimmed();
    const QString phrase = fold(raw);
    const QStringList words = phrase.split(u' ', Qt::SkipEmptyParts);
    const QString hex = codeQuery(raw);
    QHash<QString, int> order;
    if (scope) {
        for (int i = 0; i < scope->size(); ++i)
            order.insert(scope->at(i), i);
    }
    struct Match { int row, rank, order; };
    QList<Match> matches;
    for (int i = 0; i < entries_.size(); ++i) {
        const auto &entry = entries_.at(i);
        if ((!theme.isEmpty() && theme != entry.theme) || (scope && !order.contains(entry.code))) continue;
        int rank = 0;
        if (entry.text == query) {
            rank = 0;  // Includes a pasted literal space character from the spacing theme.
        } else if (!hex.isEmpty()) {
            const QStringView digits = QStringView(entry.code).mid(2);
            if (!digits.startsWith(hex)) continue;
            rank = digits.size() == hex.size() ? 0 : 1;
        } else if (!raw.isEmpty()) {
            if (entry.aliases.contains(raw, Qt::CaseInsensitive) || (!phrase.isEmpty() &&
                    (phrase == entry.foldedName || phrase == entry.foldedOfficialName))) {
                rank = 1;
            } else {
                if (words.isEmpty() || !std::all_of(words.cbegin(), words.cend(),
                        [&](const QString &word) { return entry.keywords.contains(word); })) continue;
                if (entry.foldedName.startsWith(phrase) || entry.foldedOfficialName.startsWith(phrase)) rank = 2;
                else if (entry.foldedName.contains(phrase) || entry.foldedOfficialName.contains(phrase)) rank = 3;
                else rank = 4;
            }
        } else if (!query.isEmpty() && query != raw) {
            // Pasting NBSP should identify exactly that character. Ordinary
            // surrounding ASCII spaces on a description are ignored above.
            if (query != QString(query.size(), u' ')) continue;
        }
        matches.append({i, rank, scope ? order.value(entry.code) : i});
    }
    std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        return a.rank != b.rank ? a.rank < b.rank : a.order < b.order;
    });
    QList<int> rows;
    rows.reserve(matches.size());
    for (const auto &match : matches) rows.append(match.row);
    return rows;
}
