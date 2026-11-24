#pragma once

#include "textcompanion.h"
#include "synonymentry.h"

class SynonymFile {
public:
    explicit SynonymFile(const QString &suffix) : companion_(suffix) {}
    bool openManuscript(const QString &path, QString *error = nullptr);
    bool saveAs(const QString &path, QString *error = nullptr);
    bool flush(QString *error = nullptr) { return companion_.flush(error); }
    bool hasPending() const { return companion_.hasPending(); }
    const QString &manuscriptPath() const { return companion_.manuscriptPath(); }
    const QList<SynonymEntry> &groups() const { return groups_; }

    bool add(const QString &line, bool excludeFromAutocomplete, QString *error = nullptr);
    bool add(const QString &line, QString *error = nullptr) { return add(line, false, error); }
    bool edit(int row, const QString &line, bool excludeFromAutocomplete, QString *error = nullptr);
    bool edit(int row, const QString &line, QString *error = nullptr)
    { return edit(row, line, groups_.value(row).excludeFromAutocomplete, error); }
    bool remove(int row, QString *error = nullptr);
    static bool parse(const QString &line, QStringList &group, QString *error = nullptr);
    static QString format(const QStringList &group);

private:
    bool decode(QString *error);
    bool store(QList<SynonymEntry> groups, QString *error);
    TextCompanion companion_;
    QList<SynonymEntry> groups_;
    bool readable_ = true;
};
