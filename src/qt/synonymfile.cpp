#include "synonymfile.h"
#include <QCollator>
#include <QStringDecoder>
#include <algorithm>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

// A legacy CSV group cannot start with an empty field.
const QString excludedPrefix = QStringLiteral(",no-complete,");

void alphabetize(QList<SynonymEntry> &groups)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(groups.begin(), groups.end(), [&](const SynonymEntry &a, const SynonymEntry &b) {
        return collator.compare(a.aliases.first(), b.aliases.first()) < 0;
    });
}
}

bool SynonymFile::parse(const QString &line, QStringList &group, QString *error)
{
    if (line.contains(u'\n') || line.contains(u'\r') || line.contains(QChar::Null))
        return fail(error, QStringLiteral("Each group occupies one line and must contain ordinary text."));
    QStringList fields;
    qsizetype i = 0;
    for (;;) {
        while (i < line.size() && line[i].isSpace()) ++i;
        QString field;
        if (i < line.size() && line[i] == u'"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const QChar c = line[i++];
                if (c != u'"') {
                    field += c;
                } else if (i < line.size() && line[i] == u'"') {
                    field += u'"';
                    ++i;
                } else {
                    closed = true;
                    break;
                }
            }
            if (!closed)
                return fail(error, QStringLiteral("A quoted synonym needs a closing double quote."));
            while (i < line.size() && line[i].isSpace()) ++i;
            if (i < line.size() && line[i] != u',')
                return fail(error, QStringLiteral("A comma must follow a quoted synonym."));
        } else {
            const qsizetype start = i;
            while (i < line.size() && line[i] != u',') ++i;
            field = line.mid(start, i - start).trimmed();
            if (field.contains(u'"'))
                return fail(error, QStringLiteral("Enclose a synonym containing quotes in double quotes and double each inner quote."));
        }
        if (field.trimmed().isEmpty())
            return fail(error, QStringLiteral("Enter a name for each synonym."));
        fields.append(field);
        if (i == line.size()) break;
        ++i;
    }
    group = std::move(fields);
    return true;
}

QString SynonymFile::format(const QStringList &group)
{
    QStringList fields;
    for (const auto &field : group) {
        if (field.contains(u',') || field.contains(u'"') || field.trimmed() != field) {
            QString quoted = field;
            quoted.replace(u'"', QStringLiteral("\"\""));
            fields.append(u'"' + quoted + u'"');
        } else {
            fields.append(field);
        }
    }
    return fields.join(QStringLiteral(", "));
}

bool SynonymFile::decode(QString *error)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString decoded = decoder(companion_.bytes());
    if (decoder.hasError())
        return fail(error, QStringLiteral("%1 contains invalid UTF-8.").arg(companion_.sidecarPath()));
    decoded.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    decoded.replace(u'\r', u'\n');
    QList<SynonymEntry> next;
    int number = 0;
    for (const auto line : QStringView(decoded).split(u'\n')) {
        ++number;
        if (line.trimmed().isEmpty()) continue;
        QStringList group;
        QString reason;
        const bool excluded = line.startsWith(excludedPrefix);
        const auto aliases = excluded ? line.mid(excludedPrefix.size()) : line;
        if (!parse(aliases.toString(), group, &reason))
            return fail(error, QStringLiteral("%1, line %2. %3").arg(companion_.sidecarPath()).arg(number).arg(reason));
        next.append({std::move(group), excluded});
    }
    alphabetize(next);
    groups_ = std::move(next);
    return true;
}

bool SynonymFile::openManuscript(const QString &path, QString *error)
{
    groups_.clear();
    readable_ = companion_.openManuscript(path, error) && decode(error);
    return readable_;
}

bool SynonymFile::saveAs(const QString &path, QString *error)
{
    const bool wrote = companion_.saveAs(path, error);
    // A conflicting destination leaves the source bytes pending. An empty
    // source adopts any existing sidecar belonging to the selected target.
    QString formatError;
    groups_.clear();
    readable_ = decode(&formatError);
    if (wrote && !readable_) return fail(error, formatError);
    return wrote;
}

bool SynonymFile::store(QList<SynonymEntry> next, QString *error)
{
    if (!readable_)
        return fail(error, QStringLiteral("Correct the list file and reload it before editing."));
    alphabetize(next);
    QByteArray bytes;
    for (const auto &group : next) {
        if (group.excludeFromAutocomplete) bytes += excludedPrefix.toUtf8() + ' ';
        bytes += format(group.aliases).toUtf8();
        bytes += '\n';
    }
    if (!companion_.write(bytes, error)) return false;
    groups_ = std::move(next);
    return true;
}

bool SynonymFile::add(const QString &line, bool excluded, QString *error)
{
    QStringList group;
    if (!parse(line, group, error)) return false;
    auto next = groups_;
    next.append({std::move(group), excluded});
    return store(std::move(next), error);
}

bool SynonymFile::edit(int row, const QString &line, bool excluded, QString *error)
{
    if (row < 0 || row >= groups_.size())
        return fail(error, QStringLiteral("Select an existing synonym group."));
    QStringList group;
    if (!parse(line, group, error)) return false;
    auto next = groups_;
    next[row] = {std::move(group), excluded};
    return store(std::move(next), error);
}

bool SynonymFile::remove(int row, QString *error)
{
    if (row < 0 || row >= groups_.size())
        return fail(error, QStringLiteral("Select an existing synonym group."));
    auto next = groups_;
    next.removeAt(row);
    return store(std::move(next), error);
}
