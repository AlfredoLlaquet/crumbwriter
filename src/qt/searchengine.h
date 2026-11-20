#pragma once

#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

namespace cwsearch {

enum class Mode { Text, Regex, Number };

struct Options {
    Mode mode = Mode::Text;
    bool matchCase = false;
    bool wholeWords = false;
    bool wrap = true;
    int flags() const;
    static Options fromFlags(int flags);
    static bool validFlags(int flags);
};

struct Span {
    int start = -1;
    int end = -1;
    bool found() const { return start >= 0; }
    bool operator==(const Span &other) const { return start == other.start && end == other.end; }
};

struct Matches {
    QVector<Span> spans;
    QString error;
};

struct Edit {
    Span original;
    QString replacement;
};

struct Example {
    int line;
    QString before;
    QString after;
};

struct ReplacementPlan {
    QVector<Edit> edits;
    QVector<Example> examples;
    int affectedLines = 0;
    int changes = 0;
    QString error;
};

// Positions in all public results count Qt UTF-16 units. The regex engine
// keeps the original PCRE and replacement dialect, including named captures.
class Pattern {
public:
    Pattern() = default;
    Pattern(const QString &query, Options options);
    QString error() const;
    Span nearestLiteral(const QString &text, int origin, bool backwards, bool wrap, bool &wrapped) const;
    Matches collect(const QString &text, const std::atomic_bool &cancel) const;
    QString expandOne(const QString &text, Span selected, const QString &replacement,
                      QString &error) const;
    ReplacementPlan plan(const QString &text, const QString &replacement,
                         const std::function<QString(QString)> &normalize,
                         const std::atomic_bool &cancel) const;

private:
    struct Compiled;
    std::shared_ptr<const Compiled> compiled;
};

int nearestIndex(const QVector<Span> &matches, int origin, bool backwards, bool wrap, bool &wrapped);

} // namespace cwsearch
