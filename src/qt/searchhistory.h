#pragma once

#include "searchengine.h"

class QSettings;

namespace cwsearch {

struct HistoryEntry {
    QString query;
    Options options;
};

class History {
public:
    explicit History(QSettings *settings);
    QVector<HistoryEntry> entries() const;
    void remember(const HistoryEntry &entry);
private:
    QSettings *settings;
};

} // namespace cwsearch
