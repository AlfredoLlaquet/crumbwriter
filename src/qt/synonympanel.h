#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <functional>

class QTextDocument;
class SynonymMatcher;
class SynonymResults;

class SynonymPanel final : public QWidget {
public:
    explicit SynonymPanel(QTextDocument *document, QWidget *parent = nullptr);
    void focusCharacters();
    void focusRecurring();
    bool openManuscript(const QString &path, QString *error = nullptr);
    bool saveAs(const QString &path, QString *error = nullptr);
    bool flush(QString *error = nullptr);
    bool hasPending() const;
    QStringList allAliases() const;
    void setGenres(const QStringList &genres);
    bool showGenreOccurrences(const QString &genre);
    SynonymMatcher *matcher() const { return matcher_; }

    std::function<void()> onChanged;
    std::function<QString()> manuscriptSelection;
    std::function<void(const QString &)> insertSynonym;
    std::function<void(int, int)> jumpToMatch;
    std::function<void()> returnToEditor;

private:
    class Section;
    void showOccurrences(int group);
    void rebuildMatcher();
    void changed();
    QPointer<QTextDocument> document_;
    SynonymMatcher *matcher_;
    Section *characters_;
    Section *recurring_;
    SynonymResults *results_ = nullptr;
    QStringList genres_;
};
