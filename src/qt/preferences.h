#ifndef CRUMBWRITER_PREFERENCES_H
#define CRUMBWRITER_PREFERENCES_H

#include <QFont>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <functional>

class QDialog;
class QWidget;
struct SynonymWritingSettings;

class Preferences {
public:
    explicit Preferences(const QString &file = {});
    QSettings &settings() { return settings_; }
    bool enabled(const QString &key) const;
    int saveDelay() const;
    QFont manuscriptFont() const;
    QPalette applyControlTheme() const;
    SynonymWritingSettings synonymSettings() const;
    QString lastDocument() const;
    void remember(const QString &path);
    void show(QWidget *parent);
    void setWritingFont(const QFont &font);
    std::function<void()> changed;
private:
    void set(const QString &key, const QVariant &value);
    QWidget *synonymPage(QWidget *parent);
    QSettings settings_;
    QPointer<QDialog> dialog_;
};

void showShortcutHelp(QWidget *parent);

#endif
