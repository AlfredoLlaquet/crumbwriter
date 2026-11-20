#ifndef CRUMBWRITER_PREFERENCES_H
#define CRUMBWRITER_PREFERENCES_H

#include <QFont>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <functional>

class QDialog;
class QWidget;

class Preferences {
public:
    explicit Preferences(const QString &file = {});
    QSettings &settings() { return settings_; }
    bool enabled(const QString &key) const;
    int saveDelay() const;
    QFont manuscriptFont() const;
    QPalette paletteForDesktop() const;
    QString lastDocument() const;
    void remember(const QString &path);
    void show(QWidget *parent);
    std::function<void()> changed;
private:
    void set(const QString &key, const QVariant &value);
    QSettings settings_;
    QPointer<QDialog> dialog_;
};

void showShortcutHelp(QWidget *parent);

#endif
