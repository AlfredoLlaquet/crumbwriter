#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <functional>

class QTextDocument;
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

    std::function<void()> onChanged;
    std::function<void(const QString &)> insertSynonym;
    std::function<void(int, int)> jumpToMatch;
    std::function<void()> returnToEditor;

private:
    class Section;
    void showOccurrences(const QStringList &aliases);
    void changed();
    QPointer<QTextDocument> document_;
    Section *characters_;
    Section *recurring_;
    SynonymResults *results_ = nullptr;
};
