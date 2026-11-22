#pragma once

#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>

class MainWindow;
class QFileDialog;
class QWidget;

// Drive real windows first, then capture them. A wildcard limits the output
// without changing the UI path taken to reach an individual window.
class GuiAudit {
public:
    GuiAudit(const QDir &output, const QString &theme, const QString &viewPattern);
    bool wantsView(const QString &id) const;
    void capture(const QString &id, QWidget *widget);
    void require(bool condition, const QString &message);
    void checkMainControls(MainWindow &window);
    void captureDesktopChrome(MainWindow &window);
    bool finish();

private:
    void checkFileTools(const QString &id, QFileDialog &file);
    void measure(const QString &control, double measured, double minimum);
    QDir output_;
    QString theme_;
    QRegularExpression pattern_;
    QJsonArray views_;
    QJsonArray measurements_;
    QJsonArray errors_;
    QJsonArray notes_;
};

void auditWindows(GuiAudit &audit, MainWindow &window);
void auditDesktopSearches(GuiAudit &audit, MainWindow &window);
