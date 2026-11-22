#include "editorcolors.h"
#include "mainwindow.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "promptlineedit.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QStyle>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool passed, const char *message)
{
    if (!passed) {
        std::fprintf(stderr, "Appearance regression: %s\n", message);
        std::exit(1);
    }
}

void until(const std::function<bool()> &condition, const char *message)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 15);
        if (condition()) return;
        QThread::msleep(2);
    } while (elapsed.elapsed() < 6000);
    check(false, message);
}

double luminance(const QColor &color)
{
    const auto linear = [](double s) {
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const auto first = luminance(a), second = luminance(b);
    return (qMax(first, second) + 0.05) / (qMin(first, second) + 0.05);
}

void checkPalette(const QPalette &colors)
{
    const QColor base = colors.base().color();
    for (const auto &surface : {base, EditorColors::currentPaper(base), EditorColors::inactivePaper(base)}) {
        check(contrast(colors.text().color(), surface) >= 4.5, "manuscript text is readable on all three backgrounds");
        check(contrast(EditorColors::warning(base), surface) >= 4.5, "warning ink is readable on the manuscript");
        check(contrast(EditorColors::synonymUnderline(colors), surface) >= 3.0, "synonym marks are visible");
        check(contrast(EditorColors::completionHint(colors), surface) >= 4.5, "completion hints are readable");
    }
    check(contrast(colors.highlightedText().color(), colors.highlight().color()) >= 4.5, "selection colors");
    check(contrast(colors.toolTipText().color(), colors.toolTipBase().color()) >= 4.5, "tooltips");
    check(contrast(colors.link().color(), colors.window().color()) >= 4.5, "dialog links");
    check(contrast(colors.buttonText().color(), colors.button().color()) >= 4.5, "toolbar labels");
    check(contrast(colors.placeholderText().color(), base) >= 4.5, "empty search fields");
    check(contrast(colors.text().color(), EditorColors::searchPaper(colors)) >= 4.5, "search hits");
    check(contrast(EditorColors::warning(base), EditorColors::searchPaper(colors)) >= 4.5, "number in a search hit");
}

void checkNativePrompts()
{
    QWidget container;
    QPalette native;
    native.setColor(QPalette::Window, Qt::white);
    native.setColor(QPalette::Base, Qt::white);
    native.setColor(QPalette::Text, QColor("#152b30"));
    native.setColor(QPalette::PlaceholderText, QColor(20, 40, 55, 60));
    const QColor disabled(80, 80, 80, 74);
    native.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabled);
    container.setPalette(native);
    PromptLineEdit field(&container);
    field.setPlaceholderText("Search the manuscript");
    const auto legible = [&](QPalette::ColorGroup group) {
        const QPalette p = field.palette();
        const auto hint = p.color(group, QPalette::PlaceholderText);
        return hint.alpha() == 255 && contrast(hint, p.color(group, QPalette::Base)) >= 5.0;
    };
    check(legible(QPalette::Active) && legible(QPalette::Inactive), "faint translucent native hints gain contrast");
    check(field.palette().color(QPalette::Disabled, QPalette::PlaceholderText) == disabled &&
          field.palette().text() == native.text(), "disabled hints and ordinary text preserve their native colors");

    const QColor readable(12, 27, 85, 240);
    native.setColor(QPalette::Active, QPalette::PlaceholderText, readable);
    container.setPalette(native);
    until([&] { return field.palette().color(QPalette::Active, QPalette::PlaceholderText) == readable; },
          "a readable replacement desktop hint recovers its exact hue and alpha");
    native.setColor(QPalette::Base, QColor("#202124"));
    native.setColor(QPalette::Text, QColor("#ececec"));
    native.setColor(QPalette::PlaceholderText, QColor(240, 240, 240, 40));
    container.setPalette(native);
    until([&] { return legible(QPalette::Active) && legible(QPalette::Inactive); }, "live dark desktop prompt adjustment");
    QPalette local;
    local.setColor(QPalette::Active, QPalette::PlaceholderText, Qt::yellow);
    field.setPalette(local);
    until([&] { return field.palette().color(QPalette::Active, QPalette::PlaceholderText) == Qt::yellow &&
                       legible(QPalette::Inactive); }, "readable field-specific ink survives palette refresh");
}

bool paintsLink(QLabel &label, const QColor &expected)
{
    const auto image = label.grab().toImage().convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = row[x];
            if (qAbs(qRed(pixel) - expected.red()) + qAbs(qGreen(pixel) - expected.green()) +
                qAbs(qBlue(pixel) - expected.blue()) < 18) return true;
        }
    }
    return false;
}

void key(QWidget *target, int code, Qt::KeyboardModifiers modifiers = {})
{
    QKeyEvent down(QEvent::KeyPress, code, modifiers);
    QApplication::sendEvent(target, &down);
    QKeyEvent up(QEvent::KeyRelease, code, modifiers);
    QApplication::sendEvent(target, &up);
}

QColor marginAt(const QImage &frame, int logicalY)
{
    const int y = qFloor((logicalY + 0.5) * frame.devicePixelRatio());
    check(y >= 0 && y < frame.height(), "sample belongs to the viewport");
    return frame.pixelColor(frame.width() - 3, y);
}

QColor firstWarning(const ManuscriptEditor &editor)
{
    const auto ranges = editor.document()->firstBlock().layout()->formats();
    for (const auto &span : ranges)
        if (span.format.hasProperty(QTextFormat::ForegroundBrush)) return span.format.foreground().color();
    return {};
}

bool showsMarks(const ManuscriptEditor &editor, const QPalette &palette)
{
    bool search = false, spelling = false;
    for (const auto &span : editor.extraSelections()) {
        if (span.cursor.selectedText() == "moon" && span.format.hasProperty(QTextFormat::BackgroundBrush))
            search |= span.format.background().color() == EditorColors::searchPaper(palette);
        if (span.cursor.selectedText() == "qxzzzx" && span.format.underlineStyle() == QTextCharFormat::WaveUnderline)
            spelling |= span.format.underlineColor() == EditorColors::warning(palette.base().color());
    }
    return search && spelling;
}
}

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QApplication app(argc, argv);
    checkNativePrompts();
    QTemporaryDir temp;
    check(temp.isValid(), "temporary settings and manuscript");
    const bool desktopDialogsDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const QString desktopStyle = QApplication::style()->name();
    const auto original = QApplication::palette();
    const auto dark = EditorColors::explicitTheme(original, true);
    const auto light = EditorColors::explicitTheme(dark, false);
    checkPalette(dark);
    checkPalette(light);
    auto desktop = dark;
    desktop.setColor(QPalette::Base, QColor(45, 40, 49));
    desktop.setColor(QPalette::Inactive, QPalette::Base, Qt::lightGray);
    desktop.setColor(QPalette::Inactive, QPalette::Text, Qt::black);
    QApplication::setPalette(desktop);
    Preferences prefs(temp.filePath("appearance.ini"));
    check(prefs.applyControlTheme().base() == desktop.base(), "desktop theme follows the desktop paper");
    {
        ManuscriptEditor styled;
        styled.setPaperPalette(QApplication::palette());
        styled.setPaperActive(false);
        check(styled.palette().color(QPalette::Inactive, QPalette::Base) ==
              EditorColors::inactivePaper(desktop.color(QPalette::Active, QPalette::Base)) &&
              styled.palette().color(QPalette::Inactive, QPalette::Text) == desktop.color(QPalette::Active, QPalette::Text),
              "a desktop with contrasting inactive colors keeps the writer's dark paper and light text");
    }
    prefs.settings().setValue("editor/theme", "dark");
    prefs.settings().setValue("editor/backup", false);
    QDialogButtonBox probe(QDialogButtonBox::Close | QDialogButtonBox::Apply);
    auto *standardClose = probe.button(QDialogButtonBox::Close);
    auto *iconOnly = probe.button(QDialogButtonBox::Apply);
    iconOnly->setText({});
    auto *custom = probe.addButton("Choose a color", QDialogButtonBox::ActionRole);
    QPixmap black(16, 16);
    black.fill(Qt::black);
    const QIcon graphic(black);
    for (auto *button : {standardClose, iconOnly, custom}) button->setIcon(graphic);
    QMessageBox question(QMessageBox::Question, "Question", "Remove the entry?", QMessageBox::Cancel);
    QMessageBox illustrated;
    illustrated.setIconPixmap(black);
    const auto path = temp.filePath("Moon.txt");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "create manuscript");
    file.write("Book//Crumb 8: Audit #1: qxzzzx walked on the moon. ");
    file.write(QByteArray("A goose inspected the sky. ").repeated(5));
    file.write("\nCrumb 2: Audit #3: The moon was quiet.");
    file.close();
    MainWindow window(prefs);
    check(standardClose->icon().isNull() && !standardClose->text().isEmpty() &&
          iconOnly->icon().cacheKey() == graphic.cacheKey() && custom->icon().cacheKey() == graphic.cacheKey(),
          "explicit themes remove only supplementary standard button icons");
    check(question.icon() == QMessageBox::NoIcon && question.text() == "Remove the entry?" &&
          question.standardButtons() == QMessageBox::Cancel && !illustrated.iconPixmap().isNull(),
          "explicit confirmations retain the full question and actions, and respect custom images");
    check(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs), "explicit theme uses themeable dialogs");
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    check(QApplication::style()->name().compare("Fusion", Qt::CaseInsensitive) == 0,
          "explicit theme uses a widget style that honors palette colors");
#else
    check(QApplication::style()->name() == desktopStyle, "explicit theme preserves the desktop widget style");
#endif
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    check(window.loadDocument(path), "open manuscript");
    window.resize(720, 540);
    window.activateManuscript(false);
    auto *editor = window.editor();
    until([&] { return editor->hasFocus(); }, "focused manuscript");
    editor->jumpTo(0, false);
    until([&] { return firstWarning(*editor) == EditorColors::warning(dark.base().color()); }, "dark number warning");
    const auto first = editor->document()->firstBlock();
    const auto second = first.next();
    auto lastRow = QTextCursor(first);
    lastRow.movePosition(QTextCursor::EndOfBlock);
    const int top = editor->cursorRect(QTextCursor(first)).center().y();
    const int bottom = editor->cursorRect(lastRow).center().y();
    const int neighbor = editor->cursorRect(QTextCursor(second)).center().y();
    check(bottom > top, "test crumb wraps");
    const auto activeFrame = editor->viewport()->grab().toImage();
    const auto band = marginAt(activeFrame, top), base = marginAt(activeFrame, neighbor);
    check(base == dark.base().color() && band == EditorColors::currentPaper(base), "active band and neighboring paper");
    check(marginAt(activeFrame, bottom) == band && band.lightness() > base.lightness() &&
          band.saturation() < 35, "every wrapped row has a gently lifted neutral background");

    key(editor, Qt::Key_3, Qt::ControlModifier | Qt::ShiftModifier);
    auto *panel = window.findChild<QLineEdit *>("historySearch");
    until([&] { return panel && panel->hasFocus(); }, "panel focus");
    check(marginAt(editor->viewport()->grab().toImage(), top) == band, "panel keeps the active band");
    panel->setText("Moon");
    auto *clear = panel->findChild<QToolButton *>();
    until([&] { return clear && clear->isVisible(); }, "a nonempty search has a clear button");
    const QPoint middle = clear->rect().center();
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent click(type, middle, clear->mapToGlobal(middle), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(clear, &click);
    }
    check(panel->text().isEmpty() && panel->hasFocus() && !clear->isVisible(),
          "clicking Clear search empties the query and returns the caret to the field");
    panel->setText("Moon");
    const auto selectAll = QKeySequence(QKeySequence::SelectAll)[0];
    key(panel, selectAll.key(), selectAll.keyboardModifiers());
    key(panel, Qt::Key_Backspace);
    check(panel->text().isEmpty() && !clear->isVisible(), "keyboard clearing also hides the search action");
    editor->setFocus();
    key(editor, Qt::Key_P, Qt::ControlModifier);
    auto *find = window.findChild<QLineEdit *>("cw-find");
    until([&] { return find && find->hasFocus(); }, "Find field focus");
    check(editor->palette().base().color() == base, "Find keeps active manuscript paper");
    find->setText("moon");
    until([&] { return showsMarks(*editor, dark); }, "dark live search and spelling colors");
    auto *genres = window.findChild<QTreeWidget *>("genreList");
    check(genres && genres->topLevelItemCount() > 0, "visible genre row");
    const QPersistentModelIndex genre = genres->model()->index(0, 1);
    check(genre.data(Qt::ForegroundRole).value<QBrush>().color() == EditorColors::warning(base), "dark genre warning");

    for (const char *name : {"charactersMenu", "recurringMenu"}) {
        auto *button = window.findChild<QToolButton *>(name);
        check(button && button->menu() && button->menu()->palette().window() == dark.window(),
              "panel actions menus use the dark window color");
    }
    auto *documentMenu = window.findChild<QToolButton *>("documentMenuButton");
    check(documentMenu && documentMenu->menu(), "document menu");
    for (auto *action : documentMenu->menu()->actions())
        if (action->text().contains("About Crumb Writer")) action->trigger();
    auto *dialog = window.findChild<QDialog *>("aboutDialog");
    auto *link = dialog ? dialog->findChild<QLabel *>("repositoryLink") : nullptr;
    until([&] { return link && dialog->isVisible() && dialog->palette().base() == dark.base() &&
                       paintsLink(*link, dark.link().color()); }, "About renders its link in readable dark-theme ink");
    auto *aboutButtons = dialog->findChild<QDialogButtonBox *>();
    check(aboutButtons && aboutButtons->button(QDialogButtonBox::Close)->icon().isNull(),
          "a newly opened dialog also uses text-only standard buttons");
    prefs.settings().setValue("editor/theme", "light");
    window.applyPreferences();
    until([&] { return showsMarks(*editor, light) && firstWarning(*editor) == EditorColors::warning(Qt::white); },
          "switching themes updates search, spelling and header warnings at the same positions");
    check(genre.isValid() && genre.data(Qt::ForegroundRole).value<QBrush>().color() == EditorColors::warning(Qt::white),
          "existing genre row recolored in place");
    until([&] { return dialog->palette().base() == light.base() && paintsLink(*link, light.link().color()); },
          "open About dialog and HTML link follow theme changes");
    dialog->close();

    prefs.settings().setValue("editor/theme", "dark");
    window.applyPreferences();
    window.activateManuscript(false);
    until([&] { return editor->hasFocus(); }, "return to manuscript");
    key(editor, Qt::Key_Escape);
    editor->jumpTo(0, false);
    QWidget peer;
    peer.show();
    peer.activateWindow();
    until([&] { return !window.isActiveWindow() && peer.isActiveWindow(); }, "other window activated");
    const auto inactive = editor->viewport()->grab().toImage();
    const int start = editor->cursorRect(QTextCursor(first)).center().y();
    const int next = editor->cursorRect(QTextCursor(second)).center().y();
    const auto dim = marginAt(inactive, start);
    check(dim == marginAt(inactive, next) && dim == EditorColors::inactivePaper(base) &&
          dim.lightness() > base.lightness() && dim.lightness() < band.lightness(), "inactive dark paper without band");
    check(editor->palette().text() == dark.text(), "inactive text keeps the light ink");
    peer.hide();
    prefs.settings().setValue("editor/theme", "desktop");
    window.applyPreferences();
    check(QApplication::style()->name() == desktopStyle && QApplication::palette().base() == desktop.base() &&
          QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs) == desktopDialogsDisabled,
          "Follow desktop restores the original widget style and native dialog behavior");
    QDialogButtonBox desktopButtons(QDialogButtonBox::Close);
    auto *desktopClose = desktopButtons.button(QDialogButtonBox::Close);
    check(standardClose->icon().pixmap(16).toImage() == desktopClose->icon().pixmap(16).toImage(),
          "existing dialog buttons recover the desktop's current stock icons");
    check(question.icon() == QMessageBox::Question && !illustrated.iconPixmap().isNull(),
          "Follow desktop restores standard confirmation artwork while keeping custom images");
    auto newDesktop = light;
    newDesktop.setColor(QPalette::Base, QColor(244, 247, 238));
    QApplication::setPalette(newDesktop);
    until([&] { return window.palette().color(QPalette::Active, QPalette::Base) ==
                        newDesktop.color(QPalette::Active, QPalette::Base); },
          "Follow desktop responds to a live desktop color change");
    window.hide();
    QApplication::setPalette(original);
    std::puts("Dark and light manuscript, focus, theme switching, marks and dialog colors passed.");
}
