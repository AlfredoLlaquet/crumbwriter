#pragma once

#include <QList>
#include <QStringList>
#include <memory>

class MyThes;

struct ThesaurusTerm {
    QString display;
    QString word;
};

struct ThesaurusSense {
    QString heading;
    QList<ThesaurusTerm> terms;
};

// The word index is opened only on the first lookup and shared by the windows.
class EnglishThesaurus final {
public:
    static EnglishThesaurus &instance();
    ~EnglishThesaurus();
    bool available() const { return bool(engine_); }
    bool usingBundledDictionary() const { return bundled_; }
    QList<ThesaurusSense> lookup(const QString &term) const;

private:
    EnglishThesaurus();
    std::unique_ptr<MyThes> engine_;
    bool utf8_ = false;
    bool bundled_ = false;
};
