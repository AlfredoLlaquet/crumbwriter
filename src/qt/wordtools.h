#pragma once

#include <QObject>
#include <QPointer>
#include <QTextCursor>
#include <QUrl>
#include <functional>

class ManuscriptEditor;
class ThesaurusDialog;

class WordTools final : public QObject {
public:
    enum class Reference { Dictionary, Synonyms, Thesaurus };

    explicit WordTools(ManuscriptEditor *editor);
    void selectWord();
    void showThesaurus();
    void openWeb(Reference reference);
    void closeThesaurus();
    static QUrl referenceUrl(Reference reference, const QString &term);
    std::function<void(const QString &)> onNotice;

private:
    QTextCursor wordAtCaret() const;
    QTextCursor lookupTarget() const;
    ManuscriptEditor *editor_;
    QPointer<ThesaurusDialog> dialog_;
};
