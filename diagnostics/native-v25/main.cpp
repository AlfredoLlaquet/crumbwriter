#include "mainwindow.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenuBar>
#include <QScreen>
#include <QStyle>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#include <cstdio>

static void settle()
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 250) {
        qApp->processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(8);
    }
}

static bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static int capture(const QDir &out, const QString &theme)
{
    QTemporaryDir fixture;
    if (!fixture.isValid() || !QDir().mkpath(out.path())) return 2;
    const QString path = fixture.filePath("Inspection.txt");
    if (!writeFile(path, "Inspection//Crumb 1: A Lamp: The inspector returned to the lamp.\n"
                         "Crumb 2: Audit #1: A light appeared in the eastern window.\n"
                         "Crumb 3: Audit #2: The inspector wrote a report.") ||
        !writeFile(path + ".characters", "Inspector, Archivist\nCourier, Messenger\n") ||
        !writeFile(path + ".recurring", "Lamp, Light\nWindow, Sash\n")) return 2;
    Preferences prefs(fixture.filePath("settings.ini"));
    prefs.settings().setValue("editor/theme", theme);
    prefs.settings().setValue("editor/startup-backup", false);
    MainWindow window(prefs);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    if (!window.loadDocument(path)) return 2;
    const auto screen = window.screen();
    const QRect available = screen->availableGeometry();
    window.resize(qBound(760, available.width() - 80, 1200), qBound(450, available.height() - 100, 720));
    window.activateManuscript(false);
    settle();
    QJsonObject report{{"platform", QApplication::platformName()}, {"style", window.style()->objectName()},
                       {"qt", qVersion()}, {"mode", theme}, {"system", QSysInfo::prettyProductName()},
                       {"window_active", window.isActiveWindow()}, {"font", QFontInfo(window.font()).family()}};
    QJsonArray controls;
    auto save = [&](QWidget *control, const QString &name) {
        if (!control || !control->isVisible()) return false;
        const auto pixmap = control->grab();
        if (pixmap.isNull() || !pixmap.save(out.filePath(name + ".png"))) return false;
        auto origin = control->mapTo(&window, QPoint(0, 0));
        controls.append(QJsonObject{{"file", name + ".png"}, {"x", origin.x()}, {"y", origin.y()},
            {"width", control->width()}, {"height", control->height()}, {"dpr", pixmap.devicePixelRatio()},
            {"active", control->isActiveWindow()}, {"enabled", control->isEnabled()},
            {"base", control->palette().color(QPalette::Base).name()},
            {"text", control->palette().color(QPalette::Text).name()},
            {"placeholder", control->palette().color(QPalette::PlaceholderText).name()}});
        return true;
    };
    if (!save(&window, "main") || !save(window.findChild<QToolBar *>(), "toolbar")) return 3;
    auto *document = static_cast<QToolButton *>(nullptr);
    for (auto *button : window.findChildren<QToolButton *>())
        if (button->accessibleName() == "Document menu") document = button;
    if (!save(document, "document-menu") ||
        !save(window.findChild<QToolButton *>("charactersMenu"), "characters-menu") ||
        !save(window.findChild<QToolButton *>("recurringMenu"), "recurring-menu")) return 3;
    for (const QString &name : {"charactersSearch", "recurringSearch", "historySearch", "genresSearch"}) {
        auto *field = window.findChild<QLineEdit *>(name);
        if (!save(field, name)) return 3;
        field->setText(QStringLiteral("Inspection"));
        settle();
        if (!save(field, name + "-typed")) return 3;
        field->clear();
    }
    if (auto *menuBar = window.findChild<QMenuBar *>()) {
        report["has_native_menu_bar"] = menuBar->isNativeMenuBar();
        QJsonArray titles;
        for (auto *action : menuBar->actions()) titles.append(action->text());
        report["menubar_titles"] = titles;
    }
#ifdef Q_OS_MACOS
    // Do not request Screen Recording consent. Widget screenshots above use the app's own surface.
    const bool screenAllowed = CGPreflightScreenCaptureAccess();
    report["desktop_capture_permitted"] = screenAllowed;
    if (screenAllowed) {
        const auto desktop = screen->grabWindow(0, 0, 0, screen->geometry().width(), 52);
        report["desktop_menu_capture"] = !desktop.isNull() && desktop.save(out.filePath("system-menu-bar.png"));
    }
#endif
    report["controls"] = controls;
    window.hide();
    return writeFile(out.filePath("manifest.json"), QJsonDocument(report).toJson()) ? 0 : 4;
}

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationDisplayName("Crumb Writer");
    const auto args = app.arguments();
    if (args.size() != 3 || args.at(1) != app.platformName()) return 1;
    for (const QString &theme : {"light", "dark"}) {
        const int result = capture(QDir(QDir(args.at(2)).filePath(theme)), theme);
        if (result) {
            std::fprintf(stderr, "%s capture failed: %d\n", qPrintable(theme), result);
            return result;
        }
    }
    return 0;
}
