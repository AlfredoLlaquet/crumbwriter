#ifndef CRUMBWRITER_SYMBOLCATALOG_H
#define CRUMBWRITER_SYMBOLCATALOG_H

#include <QList>
#include <QStringList>

struct WriterSymbol {
    QString text;
    QString code;
    QString name;
    QString officialName;
    QString theme;
    QStringList aliases;
    bool showSpacing = false;

    // Width markers appear only in the picker. The editor receives text.
    QString sample() const;
private:
    friend class SymbolCatalog;
    QString foldedName, foldedOfficialName, keywords;
};

// The catalog is loaded from the executable's resources on first use. Every
// manuscript window shares the same immutable entries.
class SymbolCatalog {
public:
    static const SymbolCatalog &instance();
    const QStringList &themes() const { return themes_; }
    const WriterSymbol &at(int row) const { return entries_.at(row); }
    QList<int> search(const QString &query, const QString &theme = {},
                      const QStringList *scope = nullptr) const;
private:
    SymbolCatalog();
    QList<WriterSymbol> entries_;
    QStringList themes_;
};

#endif
