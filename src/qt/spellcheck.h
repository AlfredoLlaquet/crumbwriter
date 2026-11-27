#pragma once

#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

class ManuscriptEditor;
class QWidget;

// One controller per manuscript. The US dictionary is shared; personal words,
// aliases, and ignored occurrences belong to this manuscript window.
class SpellCheck final : public QObject {
public:
    SpellCheck(ManuscriptEditor *editor, QWidget *owner);
    ~SpellCheck() override;
    void setEnabled(bool enabled);
    void setAliases(const QStringList &aliases);
    void correctAtCaret();
    void next(bool backward = false);
    void showDictionary();
    bool openManuscript(const QString &path, QString *error = nullptr);
    bool saveAs(const QString &path, QString *error = nullptr);
    bool flush(QString *error = nullptr);
    bool hasPending() const;
    std::function<void()> onChanged;
    std::function<void(const QString &)> onNotice;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    struct State;
    std::unique_ptr<State> d;
};
