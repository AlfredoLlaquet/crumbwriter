#include "thesaurus.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <cstdio>
#include "mythes.hxx"

namespace {
QString packageDirectory()
{
    const QString bin = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    return QDir::cleanPath(bin + "/../Resources/thesaurus");
#elif defined(Q_OS_WIN)
    return bin + "/thesaurus";
#else
    return QDir::cleanPath(bin + "/../share/crumbwriter/thesaurus");
#endif
}

QByteArray filename(const QString &path)
{
#ifdef Q_OS_WIN
    // The upstream Unicode _wfopen path is selected by the extended path prefix.
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (!native.startsWith(uR"(\\?\)")) {
        if (native.startsWith(uR"(\\)"))
            native = QStringLiteral(R"(\\?\UNC\)") + native.mid(2);
        else
            native.prepend(QStringLiteral(R"(\\?\)"));
    }
    return native.toUtf8();
#else
    return QFile::encodeName(path);
#endif
}

QString dictionaryKey(QString term)
{
    return term.simplified().normalized(QString::NormalizationForm_C).toLower()
        .replace(u'’', u'\'').replace(u'ʼ', u'\'').replace(u'‐', u'-').replace(u'‑', u'-');
}
}

EnglishThesaurus &EnglishThesaurus::instance()
{
    static EnglishThesaurus thesaurus;
    return thesaurus;
}

EnglishThesaurus::EnglishThesaurus()
{
    const QString bundled = packageDirectory();
    QStringList directories{bundled};
    directories += qEnvironmentVariable("CRUMBWRITER_THESAURUS_DIR").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const auto &root : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        directories << root + "/mythes" << root + "/myspell/dicts" << root + "/crumbwriter/thesaurus";
    directories << "/usr/local/share/mythes" << "/usr/share/mythes" << "/usr/local/share/myspell";
    directories.removeDuplicates();
    for (const auto &directory : directories) {
        for (const char *name : {"th_en_US_v2", "th_en_US_new", "th_en_US"}) {
            const QString base = QDir(directory).filePath(QLatin1String(name));
            const QString idx = base + ".idx", dat = base + ".dat";
            if (!QFileInfo(idx).isReadable() || !QFileInfo(dat).isReadable()) continue;
            auto engine = std::make_unique<MyThes>(filename(idx).constData(), filename(dat).constData());
            if (!engine->get_th_encoding()) continue;
            QByteArray encoding = QByteArray(engine->get_th_encoding()).toUpper().replace("-", "");
            if (encoding != "UTF8" && encoding != "ISO88591") continue;
            engine_ = std::move(engine);
            utf8_ = encoding == "UTF8";
            bundled_ = directory == bundled;
            return;
        }
    }
}

EnglishThesaurus::~EnglishThesaurus() = default;

QList<ThesaurusSense> EnglishThesaurus::lookup(const QString &term) const
{
    QList<ThesaurusSense> senses;
    if (!engine_) return senses;
    const QString key = dictionaryKey(term);
    const QByteArray bytes = utf8_ ? key.toUtf8() : key.toLatin1();
    if (bytes.isEmpty() || bytes.size() >= MAX_WD_LEN || bytes.contains('\0')) return senses;
    mentry *entries = nullptr;
    const int count = engine_->Lookup(bytes.constData(), int(bytes.size()), &entries);
    auto decode = [this](const char *text) {
        return (utf8_ ? QString::fromUtf8(text) : QString::fromLatin1(text)).trimmed();
    };
    // The LibreOffice edition labels related terms and antonyms in parentheses.
    // Show that relationship, but insert only words that belong in the manuscript.
    static const QRegularExpression relation(QStringLiteral(
        R"( \((?:generic term|similar term|related term|antonym)\)$)"));
    for (int i = 0; i < count; ++i) {
        if (!entries[i].count) continue;
        ThesaurusSense sense{decode(entries[i].defn), {}};
        QSet<QString> seen;
        for (int j = 0; j < entries[i].count; ++j) {
            const QString display = decode(entries[i].psyns[j]);
            QString word = display;
            word.remove(relation);
            if (word.isEmpty() || dictionaryKey(word) == key || seen.contains(display)) continue;
            seen.insert(display);
            sense.terms.append({display, word});
        }
        if (!sense.terms.isEmpty()) senses.append(std::move(sense));
    }
    engine_->CleanUpAfterLookup(&entries, count);
    return senses;
}
