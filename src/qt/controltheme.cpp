#include "controltheme.h"
#include "editorcolors.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QStyleHints>

namespace {
class ControlTheme final : public QObject {
public:
    using QObject::QObject;

    QPalette apply(const QString &choice)
    {
        const bool explicitTheme = choice == "light" || choice == "dark";
        const QString theme = explicitTheme ? choice : QStringLiteral("desktop");
        if (applied_ == theme) return QApplication::palette();
        if (applied_ == "desktop") {
            // A system palette has an empty resolve mask. Restoring it asks Qt
            // for today's desktop colors, even if the desktop changed meanwhile.
            desktopPalette_ = QApplication::palette();
            desktopDialogsDisabled_ = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
            desktopStyle_ = QApplication::style()->name();
#else
            const auto isBreeze = [](const QString &name) {
                return name == "breeze" || name == "breeze-dark";
            };
            const QString icons = QIcon::themeName();
            usesBreezeIcons_ = isBreeze(icons) || ((icons.isEmpty() || icons == "hicolor") &&
                                                    isBreeze(QIcon::fallbackThemeName()));
            if (usesBreezeIcons_) {
                // An empty override follows live desktop changes. Preserve a
                // named override when it differs from the platform's choice.
                setIconTheme({});
                desktopIconOverride_ = icons == QIcon::themeName() ? QString() : icons;
            }
#endif
            qApp->installEventFilter(this);
        }
        // Style and palette changes can synchronously call apply() on another window.
        applied_ = theme;
        if (!explicitTheme) qApp->removeEventFilter(this);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, explicitTheme || desktopDialogsDisabled_);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        // Windows and Aqua can paint native backgrounds independently of Qt's
        // requested ink. Fusion paints both parts of each pair from one palette.
        const QString targetStyle = explicitTheme ? QStringLiteral("Fusion") : desktopStyle_;
        if (!desktopStyle_.isEmpty() && QApplication::style()->name().compare(targetStyle, Qt::CaseInsensitive) != 0)
            QApplication::setStyle(targetStyle);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        // The window frame and Mac application menu are still OS-owned.
        if (QGuiApplication::platformName() == "windows" || QGuiApplication::platformName() == "cocoa") {
            auto scheme = Qt::ColorScheme::Unknown;
            if (explicitTheme) scheme = theme == "dark" ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light;
            QGuiApplication::styleHints()->setColorScheme(scheme);
        }
#endif
#else
        if (usesBreezeIcons_) selectBreezeIcons(theme);
#endif
        // Menus, tooltips and editors from Qt can be separate top-level widgets.
        QApplication::setPalette(explicitTheme ? EditorColors::explicitTheme(QPalette(), theme == "dark") : desktopPalette_);
        for (auto *widget : QApplication::allWidgets()) updateDialogDecoration(widget);
        return QApplication::palette();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show && watched->isWidgetType())
            updateDialogDecoration(static_cast<QWidget *>(watched));
        return false;
    }

private:
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    static void setIconTheme(const QString &name)
    {
        // Qt clears custom icon search paths when the name is reset to automatic.
        const auto paths = QIcon::themeSearchPaths();
        QIcon::setThemeName(name);
        if (QIcon::themeSearchPaths() != paths) QIcon::setThemeSearchPaths(paths);
    }

    void selectBreezeIcons(const QString &theme)
    {
        // Breeze ships separate artwork for light and dark surfaces. Keep the
        // desktop's file-type icons, including artwork supplied by applications.
        if (theme != "desktop") {
            const QString target = theme == "dark" ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
            for (const auto &root : QIcon::themeSearchPaths()) {
                if (QFileInfo::exists(QDir(root).filePath(target + "/index.theme"))) {
                    setIconTheme(target);
                    return;
                }
            }
        }
        setIconTheme(desktopIconOverride_);
    }
#endif

    void updateDialogDecoration(QWidget *widget) const
    {
        if (auto *button = qobject_cast<QPushButton *>(widget)) {
            updateStandardButton(button);
        } else if (auto *box = qobject_cast<QMessageBox *>(widget)) {
            // Confirmation prompts spell out the question and every choice.
            // A system question mark can be a fixed dark image on dark paper.
            constexpr char omitted[] = "cwOmittedQuestionIcon";
            if (applied_ != "desktop" && box->icon() == QMessageBox::Question) {
                box->setProperty(omitted, true);
                box->setIcon(QMessageBox::NoIcon);
            } else if (applied_ == "desktop" && box->property(omitted).toBool()) {
                box->setProperty(omitted, {});
                if (box->icon() == QMessageBox::NoIcon && box->iconPixmap().isNull())
                    box->setIcon(QMessageBox::Question);
            }
        }
    }

    void updateStandardButton(QPushButton *button) const
    {
        auto *box = qobject_cast<QDialogButtonBox *>(button->parentWidget());
        if (!box || button->text().isEmpty()) return;
        const auto standard = box->standardButton(button);
        if (standard == QDialogButtonBox::NoButton) return;
        // Some desktop icon sets supply black bitmap crosses even on charcoal.
        // The standard label carries the action and its keyboard mnemonic.
        constexpr char labelOnly[] = "cwLabelOnlyStandardButton";
        if (applied_ != "desktop") {
            button->setProperty(labelOnly, true);
            if (!button->icon().isNull()) button->setIcon({});
        } else if (button->property(labelOnly).toBool()) {
            button->setProperty(labelOnly, {});
            // Ask the restored desktop style for its current button decoration.
            QDialogButtonBox original;
            original.setStyle(box->style());
            button->setIcon(original.addButton(standard)->icon());
        }
    }

    QString applied_ = QStringLiteral("desktop");
    QPalette desktopPalette_;
    bool desktopDialogsDisabled_ = false;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QString desktopStyle_;
#else
    bool usesBreezeIcons_ = false;
    QString desktopIconOverride_;
#endif
};
}

QPalette configureControlTheme(const QString &choice)
{
    static QPointer<ControlTheme> controller;
    if (!controller) controller = new ControlTheme(qApp);
    return controller->apply(choice);
}
