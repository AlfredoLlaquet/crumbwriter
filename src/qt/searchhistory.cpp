#include "searchhistory.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <glib.h>

namespace cwsearch {
namespace {

const QString historyKey = QStringLiteral("search/history-v2");

QVector<HistoryEntry> decodeLegacy(const QByteArray &raw)
{
    QVector<HistoryEntry> result;
    if (raw.isEmpty())
        return result;
    GVariant *list = g_variant_parse(G_VARIANT_TYPE("a(us)"), raw.constData(), nullptr, nullptr, nullptr);
    if (!list)
        return result;
    GVariantIter iter;
    guint flags;
    const char *query;
    g_variant_iter_init(&iter, list);
    while (g_variant_iter_next(&iter, "(u&s)", &flags, &query)) {
        if (*query && Options::validFlags(flags))
            result.append({QString::fromUtf8(query), Options::fromFlags(flags)});
    }
    g_variant_unref(list);
    return result;
}

QVector<HistoryEntry> legacyEntries(QSettings *settings)
{
    GKeyFile *file = g_key_file_new();
    const auto path = QFile::encodeName(settings->fileName());
    g_key_file_load_from_file(file, path.constData(), G_KEY_FILE_NONE, nullptr);
    char *raw = g_key_file_get_string(file, "search", "recent", nullptr);
    auto entries = decodeLegacy(raw);
    g_free(raw);
    g_key_file_unref(file);
    if (!entries.isEmpty())
        return entries;
    const auto value = settings->value(QStringLiteral("search/recent"));
    // QSettings can interpret a comma-separated 1.4 INI value as a string list.
    const QString restored = value.metaType().id() == QMetaType::QStringList ?
                             value.toStringList().join(u',') : value.toString();
    return decodeLegacy(restored.toUtf8());
}

void writeEntries(QSettings *settings, const QVector<HistoryEntry> &entries)
{
    QJsonArray array;
    for (const HistoryEntry &entry : entries)
        array.append(QJsonArray{entry.query, entry.options.flags()});
    settings->setValue(historyKey, QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings->sync();
}

} // namespace

History::History(QSettings *settings) : settings(settings)
{
    if (settings && !settings->contains(historyKey)) {
        const auto existing = legacyEntries(settings);
        if (!existing.isEmpty())
            writeEntries(settings, existing);
    }
}

QVector<HistoryEntry> History::entries() const
{
    QVector<HistoryEntry> result;
    if (!settings)
        return result;
    settings->sync();
    const auto array = QJsonDocument::fromJson(settings->value(historyKey).toByteArray()).array();
    for (const auto item : array) {
        const auto values = item.toArray();
        if (values.size() != 2 || !values[0].isString() || values[0].toString().isEmpty())
            continue;
        const int flags = values[1].toInt(-1);
        if (Options::validFlags(flags))
            result.append({values[0].toString(), Options::fromFlags(flags)});
    }
    return result;
}

void History::remember(const HistoryEntry &entry)
{
    if (!settings || entry.query.isEmpty())
        return;
    auto all = entries();
    const auto same = [&](const HistoryEntry &other) {
        return other.query == entry.query && other.options.flags() == entry.options.flags();
    };
    if (!all.isEmpty() && same(all.first()))
        return;
    all.removeIf(same);
    all.prepend(entry);
    writeEntries(settings, all);
}

} // namespace cwsearch
