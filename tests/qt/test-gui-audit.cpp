#include "gui-audit.h"
#include "mainwindow.h"
#include "manuscripteditor.h"
#include "preferences.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#ifdef Q_OS_MACOS
#include <QLibrary>
#endif
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace {

void settle(int milliseconds = 20)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    } while (timer.elapsed() < milliseconds);
}

double luminance(QRgb pixel)
{
    static const auto table = [] {
        std::array<double, 256> values{};
        for (int i = 0; i < 256; ++i) {
            const double s = i / 255.0;
            values[i] = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        }
        return values;
    }();
    return 0.2126 * table[qRed(pixel)] + 0.7152 * table[qGreen(pixel)] + 0.0722 * table[qBlue(pixel)];
}

double contrast(QRgb a, QRgb b)
{
    const double x = luminance(a), y = luminance(b);
    return (qMax(x, y) + 0.05) / (qMin(x, y) + 0.05);
}

QRect pixels(const QImage &image, const QRect &logical)
{
    const qreal dpr = image.devicePixelRatio();
    const int x = qRound(logical.x() * dpr), y = qRound(logical.y() * dpr);
    return QRect(x, y, qRound((logical.x() + logical.width()) * dpr) - x,
                 qRound((logical.y() + logical.height()) * dpr) - y).intersected(image.rect());
}

QRgb background(const QImage &image, const QRect &area)
{
    QHash<QRgb, int> counts;
    QRgb winner = 0;
    int greatest = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb color = image.pixel(x, y);
            const int count = ++counts[color];
            if (count > greatest) { greatest = count; winner = color; }
        }
    }
    return winner;
}

// Require several solid ink pixels. A single stray edge or the caret must
// not be allowed to stand in for an entire word.
double paintedContrast(const QImage &ink, const QRect &logical, const QImage *unprinted = nullptr)
{
    const QRect area = pixels(ink, logical);
    if (area.isEmpty() || (unprinted && unprinted->size() != ink.size())) return 1.0;
    const QRgb paper = unprinted ? 0 : background(ink, area);
    const int count = qMax(4, qRound(4 * ink.devicePixelRatio() * ink.devicePixelRatio()));
    QList<double> strongest(count, 1.0);
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const auto ratio = contrast(ink.pixel(x, y), unprinted ? unprinted->pixel(x, y) : paper);
            if (ratio <= strongest.front()) continue;
            strongest.front() = ratio;
            std::sort(strongest.begin(), strongest.end());
        }
    }
    return strongest.front();
}

QImage actualControl(QWidget *control)
{
    // An auto-raised button inherits the toolbar surface. Painting a detached
    // button could measure its icon against a completely different background.
    QWidget *top = control->window();
    const QRect rect(control->mapTo(top, QPoint()), control->size());
    return top->grab(rect).toImage().convertToFormat(QImage::Format_RGB32);
}

QRect selectedWordArea(const QLineEdit &field, int left, int right)
{
    // Whitespace above/below a selection can still use the unselected paper.
    const int height = field.fontMetrics().height();
    return QRect(left, (field.height() - height) / 2 + 2, right - left + 1, height - 4);
}

double clearButtonContrast(QLineEdit &field)
{
    for (auto *button : field.findChildren<QAbstractButton *>()) {
        if (button->isVisibleTo(&field))
            return paintedContrast(actualControl(button), button->rect().adjusted(2, 2, -2, -2));
    }
    return 1.0;
}

QRect textBounds(const QLabel *label)
{
    QRect contents = label->contentsRect().marginsRemoved(QMargins(label->margin(), label->margin(),
                                                                   label->margin(), label->margin()));
    const int flags = label->alignment().toInt() | Qt::TextShowMnemonic |
                      (label->wordWrap() ? Qt::TextWordWrap : Qt::TextSingleLine);
    return label->fontMetrics().boundingRect(contents, flags, label->text()).intersected(contents);
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.flush();
}

bool createManuscript(const QString &path)
{
    return writeFile(path,
        "Moon Logbook//Crumb 1: Dispatch #1: Ada Finch visits the moon. Inspector Goose records the bright horizon.\n"
        "Crumb 2: A Clear Night: The observatory has only one window. Through it the lunar surface looks like a sea.\n"
        "Crumb 3: Dispatch #2: Captain Finch hangs a new map in the tower. Ada Finch checks the marks twice.\n"
        "Crumb 4: First Light: Inspector Goose circles one mispellled word in the logbook.\n"
        "Crumb 5: Dispatch #3: The moon shines through the window. The observatory closes at dawn.\n"
        "Crumb 6: Returning: Ada Finch takes the logbook to the tower.\n") &&
        writeFile(path + ".characters", "Ada Finch, Captain Finch\nInspector Goose, Goose\n") &&
        writeFile(path + ".recurring", "moon, lunar surface\nobservatory, tower\n") &&
        writeFile(path + ".dictionary", "Zorbular\n") &&
        writeFile(path + ".bookmarks", R"({"version":1,"bookmarks":[
            {"category":"Scenes","name":"Arrival","line":1},
            {"category":"Scenes","name":"Departure","line":4}]})");
}

} // namespace

GuiAudit::GuiAudit(const QDir &output, const QString &theme, const QString &viewPattern)
    : output_(output), theme_(theme), pattern_(QRegularExpression::wildcardToRegularExpression(viewPattern))
{
    require(output_.mkpath("."), "Create the " + theme_ + " screenshot directory");
}

bool GuiAudit::wantsView(const QString &id) const
{
    return pattern_.match(id).hasMatch();
}

void GuiAudit::require(bool condition, const QString &message)
{
    if (condition) return;
    errors_.append(message);
    std::fprintf(stderr, "%s: %s\n", qPrintable(theme_), qPrintable(message));
}

void GuiAudit::measure(const QString &control, double measured, double minimum)
{
    measurements_.append(QJsonObject{{"control", control}, {"contrast", measured}, {"minimum", minimum}});
    require(measured + 0.01 >= minimum,
            QString("%1 painted contrast %2:1 (minimum %3:1)").arg(control).arg(measured, 0, 'f', 2).arg(minimum));
}

void GuiAudit::capture(const QString &id, QWidget *widget)
{
    if (!wantsView(id)) return;
    require(widget && widget->isVisible(), "Visible view: " + id);
    if (!widget || !widget->isVisible()) return;
    settle();
    const QImage image = widget->grab().toImage().convertToFormat(QImage::Format_RGB32);
    require(!image.isNull() && image.width() > 8 && image.height() > 8, "Rendered image: " + id);
    if (image.isNull()) return;
    require(image.save(output_.filePath(id + ".png")), "Write " + id + ".png");
    views_.append(QJsonObject{{"id", id}, {"width", image.width()}, {"height", image.height()},
                              {"devicePixelRatio", image.devicePixelRatio()}, {"style", widget->style()->objectName()}});
    auto checkSurface = [&](const QRect &area) {
        if (theme_ == "desktop") return;
        const auto paper = background(image, pixels(image, area));
        require((qGray(paper) < 128) == (theme_ == "dark"),
                id + " paints the selected " + theme_ + " background");
    };
    if (qobject_cast<QDialog *>(widget) && widget->width() > 30)
        checkSurface(QRect(12, 3, widget->width() - 24, 3));
    // Child windows can still belong to the main window's QObject tree. Only
    // test text actually painted inside this picture, including its clip area.
    auto inspect = [&](QWidget *child, const QRect &text, const QString &name, double minimum = 4.5) {
        if (child->window() != widget->window() || !child->isVisibleTo(widget) || !child->isEnabled()) return;
        QRect visible = child->visibleRegion().boundingRect().intersected(text);
        if (visible.width() < 8 || visible.height() < child->fontMetrics().ascent()) return;
        visible.translate(child->mapTo(widget, QPoint()));
        if (!widget->rect().contains(visible)) return;
        measure(id + "/" + name, paintedContrast(image, visible), minimum);
    };
    if (auto *file = qobject_cast<QFileDialog *>(widget)) {
        for (const char *name : {"backButton", "forwardButton", "toParentButton"}) {
            auto *button = file->findChild<QToolButton *>(name);
            require(button && button->isVisibleTo(file), id + "/" + name + " file navigation button");
            if (!button || !button->isEnabled()) continue;
            const QSize extent = button->iconSize().boundedTo(button->size());
            QRect glyph(QPoint(), extent);
            glyph.moveCenter(button->rect().center());
            measure(id + "/" + name, paintedContrast(actualControl(button), glyph), 3.0);
        }
    }
    for (auto *label : widget->findChildren<QLabel *>()) {
        const QString text = label->text();
        if (qobject_cast<QMessageBox *>(widget) && !label->pixmap().isNull()) {
            inspect(label, label->contentsRect(), "message icon", 3.0);
            continue;
        }
        if (label->objectName() == "repositoryLink") {
            inspect(label, label->contentsRect().adjusted(2, 2, -2, -2), "repository link");
            continue;
        }
        if (text.trimmed().size() < 3 || label->textFormat() == Qt::RichText ||
            (label->textFormat() == Qt::AutoText && Qt::mightBeRichText(text))) continue;
        inspect(label, textBounds(label), text.left(42));
    }
    for (auto *button : widget->findChildren<QPushButton *>()) {
        const QString text = button->text();
        if (text.size() < 2 || button->menu()) continue;
        auto rect = button->fontMetrics().boundingRect(button->rect(), Qt::AlignCenter |
                          Qt::TextShowMnemonic | Qt::TextSingleLine, text);
        if (!button->icon().isNull()) {
            const auto size = button->iconSize();
            const int start = (button->width() - rect.width() - 4 - size.width()) / 2;
            const QRect symbol(start, (button->height() - size.height()) / 2, size.width(), size.height());
            inspect(button, symbol, text + "/icon", 3.0);
            rect.moveLeft(symbol.right() + 5);
        }
        inspect(button, rect, text);
    }
    for (auto *check : widget->findChildren<QCheckBox *>()) {
        if (check->text().isEmpty()) continue;
        QStyleOptionButton option;
        option.initFrom(check);
        const auto contents = check->style()->subElementRect(QStyle::SE_CheckBoxContents, &option, check);
        const auto text = check->fontMetrics().boundingRect(contents, Qt::AlignLeft | Qt::AlignVCenter |
                                                            Qt::TextShowMnemonic, check->text());
        inspect(check, text.intersected(contents), check->text());
    }
    for (auto *combo : widget->findChildren<QComboBox *>()) {
        if (combo->isEditable() || combo->currentText().isEmpty() || !combo->itemIcon(combo->currentIndex()).isNull())
            continue;
        QStyleOptionComboBox option;
        option.initFrom(combo);
        const auto contents = combo->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                             QStyle::SC_ComboBoxEditField, combo);
        const auto text = combo->fontMetrics().boundingRect(contents, Qt::AlignLeft | Qt::AlignVCenter,
                                                            combo->currentText());
        inspect(combo, text.intersected(contents), combo->currentText());
    }
    if (id == "symbols" || id == "bookmarks-list") {
        auto *field = widget->findChild<QLineEdit *>(id == "symbols" ? "symbolSearch" : "bookmarkFilter");
        require(field && field->isVisible() && !field->text().isEmpty(), id + " shows a filled search");
        if (field) {
            measure(id + "/clear button", clearButtonContrast(*field), 3.0);
            // Keep the chosen row and filtering model intact during this pair
            // of grabs. The resulting screenshot still shows the filled field.
            const QSignalBlocker blocker(field);
            const auto value = field->text(), placeholder = field->placeholderText();
            const int caret = field->cursorPosition(), start = field->selectionStart();
            const int length = field->selectedText().size();
            field->setCursorPosition(0);
            const auto typed = actualControl(field);
            field->clear();
            const auto ink = actualControl(field);
            const int left = field->hasFocus() ? field->inputMethodQuery(Qt::ImCursorRectangle).toRect().right() + 1 : 4;
            const auto text = field->rect().adjusted(qMax(4, left), 3, -24, -3);
            field->setPlaceholderText({});
            const auto blank = actualControl(field);
            field->setPlaceholderText(placeholder);
            field->setText(value);
            if (start >= 0)
                field->setSelection(caret == start ? start + length : start, caret == start ? -length : length);
            else
                field->setCursorPosition(caret);
            measure(id + "/search text", paintedContrast(typed, text, &blank), 4.5);
            if (!placeholder.isEmpty())
                measure(id + "/placeholder", paintedContrast(ink, text, &blank), 4.5);
        }
    }
    // These two compact toolbar-style controls were especially faint in Aqua.
    // Keep the focus ring outside the sample, which measures the actual label.
    for (auto *button : widget->findChildren<QToolButton *>()) {
        const bool dismiss = button->accessibleName() == "Dismiss message";
        if (!dismiss && button->objectName() != "genreOrder") continue;
        if (button->window() != widget->window() || !button->isVisibleTo(widget)) continue;
        const auto box = button->fontMetrics().boundingRect(button->rect(), Qt::AlignCenter, button->text());
        const QString name = id + (dismiss ? "/dismiss notice" : "/sort order");
        measure(name, paintedContrast(actualControl(button), box), dismiss ? 3.0 : 4.5);
        if (id == (dismiss ? "manuscript-notice" : "main")) {
            auto *previousFocus = QApplication::focusWidget();
            button->setFocus(Qt::TabFocusReason);
            settle();
            require(button->hasFocus(), name + " accepts keyboard focus");
            measure(name + "/focused", paintedContrast(actualControl(button), box), dismiss ? 3.0 : 4.5);
            if (previousFocus) previousFocus->setFocus(Qt::OtherFocusReason);
        }
    }
    if (auto *menu = qobject_cast<QMenu *>(widget)) {
        bool checkedSurface = false;
        for (auto *action : menu->actions()) {
            const auto rect = menu->actionGeometry(action).adjusted(24, 3, -15, -3);
            if (action->isSeparator() || !action->isEnabled() || rect.isEmpty()) continue;
            // Menus can scroll. Measure only rows that are actually on screen.
            if (!menu->rect().contains(rect)) continue;
            if (!checkedSurface && action != menu->activeAction()) {
                checkSurface(rect);
                checkedSurface = true;
            }
            measure(id + "/" + action->text().section('\t', 0, 0), paintedContrast(image, rect), 4.5);
        }
    }
}

void GuiAudit::checkMainControls(MainWindow &window)
{
    if (!wantsView("main") && !wantsView("panel-searches")) return;
    auto *bar = window.findChild<QToolBar *>();
    require(bar != nullptr, "The manuscript toolbar exists");
    if (!bar) return;
    int actionCount = 0;
    for (auto *action : bar->actions()) {
        auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action));
        // Its QWidgetAction has a title for the toolbar overflow menu, while
        // the main surface deliberately shows only the three-stroke icon.
        if (!button || button->objectName() == "documentMenuButton" || action->text().isEmpty()) continue;
        ++actionCount;
        const QString label = "toolbar/" + action->text();
        require(button->isVisible() && button->isEnabled(), label + " is visible and enabled");
        const auto image = actualControl(button);
        const int icon = button->iconSize().width();
        const int middle = button->height() / 2;
        const auto text = button->rect().adjusted(icon + 6, 4, -3, -4);
        measure(label + "/text", paintedContrast(image, text), 4.5);
        measure(label + "/icon", paintedContrast(image, QRect(3, middle - icon / 2, icon, icon)), 3.0);
    }
    require(actionCount == 4, "All four labeled toolbar actions were measured");

    for (const auto *name : {"documentMenuButton", "charactersMenu", "recurringMenu"}) {
        auto *button = window.findChild<QToolButton *>(name);
        require(button && button->isVisible() && button->menu(), QString("Visible menu button: ") + name);
        if (!button) continue;
        const auto image = actualControl(button);
        require(button->width() >= 26 && button->height() >= 26, QString(name) + " has a usable target");
        // Aqua can shade each toolbar row differently. Subtract the same
        // button painted with an empty icon, so gradients and borders cancel.
        // Equal icon sizes preserve the button's normal style and geometry.
        const auto original = button->icon();
        QPixmap transparent(button->iconSize());
        transparent.fill(Qt::transparent);
        button->setIcon(QIcon(transparent));
        const auto paper = actualControl(button);
        button->setIcon(original);
        require(image.size() == paper.size(), QString(name) + " keeps the same bounds with a blank icon");
        if (image.size() != paper.size()) continue;
        QRect square(QPoint(), button->iconSize().boundedTo(button->size()));
        square.moveCenter(button->rect().center());
        const QRect glyph = pixels(image, square);
        // The three strokes must cross the same three columns. This rejects
        // a Qt application logo, a row of dots, and a lone disclosure arrow.
        for (const double fraction : {0.25, 0.50, 0.75}) {
            const int x = glyph.left() + qRound((glyph.width() - 1) * fraction);
            int strokes = 0;
            bool inside = false;
            for (int y = glyph.top(); y <= glyph.bottom(); ++y) {
                const bool ink = contrast(image.pixel(x, y), paper.pixel(x, y)) >= 3.0;
                if (ink && !inside) ++strokes;
                inside = ink;
            }
            require(strokes == 3, QString("%1 shows %2 strokes at %3% of its icon (expected three)")
                                      .arg(name).arg(strokes).arg(qRound(100 * fraction)));
        }
    }

    for (const auto *name : {"charactersSearch", "recurringSearch", "historySearch", "genresSearch"}) {
        auto *field = window.findChild<QLineEdit *>(name);
        require(field && field->isVisible() && field->isEnabled(), QString("Visible search field: ") + name);
        if (!field) continue;
        const auto placeholder = field->placeholderText();
        require(!placeholder.isEmpty() && field->text().isEmpty(), QString("Empty search prompt: ") + name);
        const auto prompt = actualControl(field);
        field->setPlaceholderText({});
        const auto blank = actualControl(field);
        field->setText("Moon");
        const auto filled = actualControl(field);
        field->clear();
        field->setPlaceholderText(placeholder);
        const auto text = field->rect().adjusted(4, 3, -20, -3);
        const auto paper = background(blank, pixels(blank, text));
        double minimumSelection = 4.5;
        if (theme_ == "desktop") {
            // Native styles choose their own placeholder and selection ink.
            // Paint a stock field on the same display and font, including
            // real keyboard focus. Keep a floor against disappearing text.
            auto *originalFocus = QApplication::focusWidget();
            QLineEdit native(&window);
            native.setPalette(QApplication::palette("QLineEdit"));
            native.setFont(field->font());
            native.setClearButtonEnabled(true);
            native.setGeometry(QRect(field->mapTo(&window, QPoint()), field->size()));
            native.setPlaceholderText(placeholder);
            native.show();
            native.raise();
            const auto nativePrompt = actualControl(&native);
            native.setPlaceholderText({});
            const auto nativeBlank = actualControl(&native);
            const double reference = paintedContrast(nativePrompt, text, &nativeBlank);
            measure(QString(name) + "/desktop reference placeholder", reference, 1.0);
            native.setFocus(Qt::TabFocusReason);
            settle();
            const int start = qMax(text.left(), native.inputMethodQuery(Qt::ImCursorRectangle).toRect().right() + 1);
            native.setText("Moon");
            const int end = native.inputMethodQuery(Qt::ImCursorRectangle).toRect().left() - 1;
            settle(220); // Let the platform's clear-button fade reach its final opacity.
            const auto clear = clearButtonContrast(native);
            // A desktop can omit this icon altogether. Record what its stock
            // control drew, but still require a visible application button.
            measure(QString(name) + "/desktop reference clear button", clear, 1.0);
            native.selectAll();
            const auto selection = paintedContrast(actualControl(&native), selectedWordArea(native, start, end));
            measure(QString(name) + "/desktop reference selection", selection, 1.5);
            minimumSelection = qBound(1.5, selection * 0.90, 4.5);
            native.hide();
            if (originalFocus) originalFocus->setFocus(Qt::OtherFocusReason);
        } else {
            require((qGray(paper) < 128) == (theme_ == "dark"), QString(name) + " paints the selected background");
        }
        measure(QString(name) + "/placeholder", paintedContrast(prompt, text, &blank), 4.5);
        measure(QString(name) + "/entry", paintedContrast(filled, text, &blank), 4.5);

        auto *previousFocus = QApplication::focusWidget();
        field->setFocus(Qt::TabFocusReason);
        settle();
        require(field->hasFocus(), QString(name) + " receives keyboard focus");
        const auto focusPrompt = actualControl(field);
        const int emptyCaretRight = field->inputMethodQuery(Qt::ImCursorRectangle).toRect().right();
        field->setPlaceholderText({});
        const auto focusBlank = actualControl(field);
        field->setText("Moon");
        settle(220);
        const auto focusInk = actualControl(field);
        const int filledCaretLeft = field->inputMethodQuery(Qt::ImCursorRectangle).toRect().left();
        auto promptArea = text;
        promptArea.setLeft(qMax(text.left(), emptyCaretRight + 1));
        auto typedArea = promptArea;
        typedArea.setRight(qMin(text.right(), filledCaretLeft - 1));
        measure(QString(name) + "/focused placeholder", paintedContrast(focusPrompt, promptArea, &focusBlank), 4.5);
        measure(QString(name) + "/focused entry", paintedContrast(focusInk, typedArea, &focusBlank), 4.5);
        measure(QString(name) + "/clear button", clearButtonContrast(*field), 3.0);
        field->selectAll();
        const auto selected = selectedWordArea(*field, typedArea.left(), typedArea.right());
        measure(QString(name) + "/selected entry", paintedContrast(actualControl(field), selected), minimumSelection);
        field->clear();
        field->setPlaceholderText(placeholder);
        if (previousFocus) previousFocus->setFocus(Qt::OtherFocusReason);
    }
    if (wantsView("panel-searches")) {
        auto *previousFocus = QApplication::focusWidget();
        QList<QLineEdit *> fields;
        for (const auto *name : {"charactersSearch", "recurringSearch", "historySearch", "genresSearch"}) {
            if (auto *field = window.findChild<QLineEdit *>(name)) fields.append(field);
        }
        if (fields.size() == 4) {
            const QStringList queries{"Ada", "moon", "Dispatch", "Dispatch"};
            for (int i = 0; i < fields.size(); ++i) fields[i]->setText(queries[i]);
            fields.front()->setFocus(Qt::TabFocusReason);
            fields.front()->selectAll();
            settle(220);
            capture("panel-searches", &window);
            for (auto *field : fields) field->clear();
        }
        if (previousFocus) previousFocus->setFocus(Qt::OtherFocusReason);
    }
}

void GuiAudit::captureDesktopChrome(MainWindow &window)
{
    const auto platform = QGuiApplication::platformName();
    const bool title = wantsView("native-title-bar") &&
                       (platform == "windows" || platform == "cocoa" || platform == "xcb");
    const bool menu = wantsView("macos-menu-bar") && platform == "cocoa";
    if (!title && !menu) return;
    if (!window.isActiveWindow()) {
        notes_.append("The test manuscript was not the active window. Desktop chrome was not captured.");
        return;
    }
#ifdef Q_OS_MACOS
    // Never trigger a screen-recording privacy prompt on a fresh Mac runner.
    QLibrary graphics("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics");
    auto preflight = reinterpret_cast<bool (*)()>(graphics.resolve("CGPreflightScreenCaptureAccess"));
    if (!preflight || !preflight()) {
        notes_.append("The desktop does not grant screen recording. The macOS native menu roles are checked separately.");
        return;
    }
#endif
    auto *screen = window.screen();
    const QRect display = screen->geometry();
    const auto saveStrip = [&](const QString &id, QRect area) {
        area = area.intersected(display).translated(-display.topLeft());
        if (area.width() < 100 || area.height() < 12 || area.height() > 80) {
            notes_.append(id + ": this desktop provides no visible decoration strip at the test window.");
            return;
        }
        const auto image = screen->grabWindow(0, area.x(), area.y(), area.width(), area.height())
                               .toImage().convertToFormat(QImage::Format_RGB32);
        if (image.isNull() || paintedContrast(image, QRect(QPoint(), area.size())) < 2.0) {
            notes_.append(id + ": desktop readback did not return a distinguishable decoration.");
            return;
        }
        require(image.save(output_.filePath(id + ".png")), "Write " + id);
        views_.append(QJsonObject{{"id", id}, {"width", image.width()}, {"height", image.height()},
                                  {"devicePixelRatio", image.devicePixelRatio()}, {"style", "system decoration"}});
    };
    if (title) {
        const auto client = window.mapToGlobal(QPoint());
        const int top = window.frameGeometry().top();
        saveStrip("native-title-bar", QRect(client.x() + 6, top + 1, window.width() - 12, client.y() - top - 2));
    }
    if (menu) {
        const int height = screen->availableGeometry().top() - display.top();
        saveStrip("macos-menu-bar", QRect(display.topLeft(), QSize(qMin(540, display.width()), height)));
    }
}

bool GuiAudit::finish()
{
    require(!views_.isEmpty(), "The requested view pattern captured at least one window");
    const QJsonObject report{{"platform", QGuiApplication::platformName()}, {"style", QApplication::style()->objectName()},
                             {"os", QSysInfo::prettyProductName()},
                             {"qt", qVersion()}, {"theme", theme_}, {"views", views_}, {"measurements", measurements_},
                             {"notes", notes_}, {"errors", errors_}};
    const bool written = writeFile(output_.filePath("report.json"), QJsonDocument(report).toJson());
    require(written, "Write report.json");
    std::printf("%s: %d images, %d painted contrast measurements, %d failures\n",
                qPrintable(theme_), int(views_.size()), int(measurements_.size()), int(errors_.size()));
    return errors_.isEmpty();
}

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName("CrumbWriterGuiAudit");
    QApplication::setApplicationDisplayName("Crumb Writer");
    QApplication::setWindowIcon(QIcon(":/icons/crumbwriter.png"));
    QApplication::setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"output", "Save screenshots and a JSON report for each theme.", "directory"});
    parser.addOption({"platform-name", "Require this Qt window system (xcb, windows or cocoa).", "platform"});
    parser.addOption({"theme", "Limit the audit to light, dark or the default desktop overview and searches.", "theme"});
    parser.addOption({"view", "Limit measured views and images to a shell wildcard.", "pattern", "*"});
    parser.process(app);
    if (parser.isSet("platform-name") && parser.value("platform-name") != QGuiApplication::platformName()) {
        std::fprintf(stderr, "Expected native platform %s, got %s\n", qPrintable(parser.value("platform-name")),
                     qPrintable(QGuiApplication::platformName()));
        return 2;
    }
    QStringList themes{"light", "dark", "desktop"};
    if (parser.isSet("theme")) {
        if (!themes.contains(parser.value("theme"))) return 2;
        themes = {parser.value("theme")};
    } else {
        const QRegularExpression requested(QRegularExpression::wildcardToRegularExpression(parser.value("view")));
        const QStringList desktopViews{"main", "panel-searches", "bookmarks-list", "symbols",
                                       "macos-menu-bar", "native-title-bar"};
        if (std::none_of(desktopViews.cbegin(), desktopViews.cend(), [&](const QString &id) {
                return requested.match(id).hasMatch();
            }))
            themes.removeAll("desktop");
    }
    const auto desktopStyle = QApplication::style()->objectName();
    const bool desktopDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const QDir output(parser.isSet("output") ? parser.value("output") : temporary.filePath("screenshots"));
    bool passed = true;
    for (const auto &theme : themes) {
        const QDir files(temporary.filePath(theme));
        const QString path = files.filePath("Moon Logbook.txt");
        if (!files.mkpath(".") || !createManuscript(path)) return 2;
        GuiAudit audit(QDir(output.filePath(theme)), theme, parser.value("view"));
        Preferences preferences(files.filePath("settings.ini"));
        preferences.settings().setValue("editor/theme", theme);
        preferences.settings().setValue("editor/backup", false);
        preferences.settings().setValue("editor/live-save", false);
        MainWindow window(preferences);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        preferences.changed = [&] { window.applyPreferences(); };
        audit.require(window.loadDocument(path), "Open the sample manuscript");
        window.resize(QSize(1100, 720).boundedTo(window.screen()->availableGeometry().size()));
        window.activateManuscript(false);
        window.editor()->jumpTo(0, false);
        settle(180);
        if (theme == "desktop") {
            audit.require(QApplication::style()->objectName() == desktopStyle, "Follow desktop restores the native widget style");
            audit.require(QApplication::testAttribute(Qt::AA_DontUseNativeDialogs) == desktopDialogsDisabled,
                          "Follow desktop restores the native dialog policy");
        }
        audit.capture("main", &window);
        audit.checkMainControls(window);
        audit.captureDesktopChrome(window);
        if (theme == "desktop") auditDesktopSearches(audit, window);
        else auditWindows(audit, window);
        window.hide();
        passed = audit.finish() && passed;
    }
    return passed ? 0 : 1;
}
