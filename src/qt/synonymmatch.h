#pragma once

#include "synonymentry.h"
#include <QObject>
#include <QStringList>
#include <memory>
#include <optional>

class QTextBlock;
class QTextDocument;

enum class NameSource { Characters, Recurring, Genre };

struct SynonymGroup {
    QStringList aliases;
    NameSource source = NameSource::Characters;
};

// Absolute UTF-16 manuscript offsets; end is exclusive. Several groups
// may use exactly the same alias. Keep their identities for navigation.
struct SynonymMention {
    int start, end;
    QList<int> groups;
};

struct SynonymSuggestion {
    QString alias;
    QString suffix;
    int start;
};

class SynonymMatcher final : public QObject {
public:
    explicit SynonymMatcher(QTextDocument *document, QObject *parent = nullptr);
    ~SynonymMatcher() override;
    void setGroups(const QList<SynonymEntry> &characters, const QList<SynonymEntry> &recurring,
                   const QStringList &genres = {});
    const QList<SynonymGroup> &groups() const;
    QList<SynonymMention> mentions(const QTextBlock &block) const;
    std::optional<SynonymMention> mentionAt(int position) const;
    std::optional<SynonymMention> neighbor(const SynonymMention &origin, int group, bool backward) const;
    // The chooser asks for the full sorted list. A limit of two lets typing
    // distinguish a single result from ambiguity without enumerating a subtree.
    QList<SynonymSuggestion> completions(int position, bool characters, bool recurring,
                                         int minimumPrefix = 1, int limit = 0) const;

private:
    struct Data;
    std::unique_ptr<Data> d;
};
