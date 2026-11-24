#pragma once

#include <QStringList>

struct SynonymEntry {
    QStringList aliases;
    bool excludeFromAutocomplete = false;

    bool operator==(const SynonymEntry &other) const
    {
        return aliases == other.aliases && excludeFromAutocomplete == other.excludeFromAutocomplete;
    }
};
