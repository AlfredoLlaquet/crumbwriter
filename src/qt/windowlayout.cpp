#include "windowlayout.h"
#include "layoutparts.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QWindow>

namespace {
const QString group = QStringLiteral("layouts/v1/");

QList<WindowLayouts *> &controllers()
{
    static QList<WindowLayouts *> live;
    return live;
}

QVariantMap geometryOf(const QWidget *window)
{
    const QScreen *screen = window->screen();
    return {{"size", window->size()}, {"position", window->frameGeometry().topLeft()},
            {"screen", screen ? screen->name() : QString()}};
}

bool manuscriptWindow(const QWidget *window)
{
    return window->objectName() == QStringLiteral("manuscriptWindow");
}

bool anotherManuscriptIsVisible(const QWidget *window)
{
    const auto windows = QApplication::topLevelWidgets();
    for (const auto *other : windows)
        if (other != window && other->isVisible() && manuscriptWindow(other)) return true;
    return false;
}

bool hasFloatingBounds(const QWidget *window)
{
    if (!window->screen() || (window->windowState() &
        (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen))) return false;
    const QRect work = window->screen()->availableGeometry();
    const QRect frame = window->frameGeometry();
    // KWin/Krohnkite gives normal windows tile rectangles; Qt's window state
    // alone cannot distinguish them. Include window decorations and allow for
    // compositor gaps and rounding. Wayland does not publish global positions.
    const int gap = qBound(4, qMin(work.width(), work.height()) / 60, 20);
    const bool positionsKnown = !QGuiApplication::platformName().startsWith("wayland");
    const bool fillsWidth = qAbs(frame.width() - work.width()) <= 2 * gap &&
        (!positionsKnown || qAbs(frame.left() - work.left()) <= gap);
    const bool fillsHeight = qAbs(frame.height() - work.height()) <= 2 * gap &&
        (!positionsKnown || qAbs(frame.top() - work.top()) <= gap);
    if (fillsWidth || fillsHeight) return false;
    // A stacked edge tile can fill a half or third of each axis. Do not
    // promote a flush grid cell into the explicit startup position of a
    // future manuscript. Normally positioned floating windows keep their
    // size and location, even if their dimensions resemble a grid cell.
    const bool atSide = qAbs(frame.left() - work.left()) <= gap || qAbs(frame.right() - work.right()) <= gap;
    const bool atEnd = qAbs(frame.top() - work.top()) <= gap || qAbs(frame.bottom() - work.bottom()) <= gap;
    auto subdivision = [gap](int length, int extent) {
        for (int divisor : {2, 3, 4})
            if (qAbs(length - extent / divisor) <= 2 * gap) return true;
        return false;
    };
    return !(positionsKnown && atSide && atEnd &&
             (subdivision(frame.width(), work.width()) || subdivision(frame.height(), work.height())));
}

// Initial geometry is a hint before mapping. A tiled window manager can
// assign a completely different rectangle. Once mapped, never reposition
// the window during a resize or in response to a changed desktop layout.
void applyGeometry(QWidget *window, const QVariantMap &saved)
{
    const QSize size = saved.value("size").toSize();
    if (!size.isValid() || size.isEmpty()) return;
    QScreen *screen = window->screen();
    bool sameScreen = false;
    for (QScreen *candidate : QGuiApplication::screens()) {
        if (candidate->name() == saved.value("screen").toString()) {
            screen = candidate;
            sameScreen = true;
            break;
        }
    }
    if (!screen) return;
    const QRect available = screen->availableGeometry();
    const QSize bounded = size.boundedTo(available.size());
    window->resize(bounded);
    if (sameScreen && saved.contains("position") && !QGuiApplication::platformName().startsWith("wayland")) {
        const QPoint p = saved.value("position").toPoint();
        window->move(qBound(available.left(), p.x(), available.right()-bounded.width()+1),
                     qBound(available.top(), p.y(), available.bottom()-bounded.height()+1));
    }
}

bool belongsToLayout(QWidget *widget, QWidget *window)
{
    if (widget->window() != window) return false;
    for (auto *p = widget->parentWidget(); p && p != window; p = p->parentWidget())
        if (p->objectName() == QStringLiteral("inactiveTabPanels")) return false;
    return true;
}

QString dialogKey(QWidget *window)
{
    if (qobject_cast<QMessageBox *>(window)) return {}; // Platform-owned prompt geometry.
    if (auto *file = qobject_cast<QFileDialog *>(window))
        return file->acceptMode() == QFileDialog::AcceptSave ? "saveFileDialog" : "openFileDialog";
    return qobject_cast<QDialog *>(window) ? window->objectName() : QString();
}
}

class WindowLayoutState final : public QObject {
public:
    WindowLayoutState(QWidget *window, const QString &id, QSettings &settings, WindowLayouts *owner)
        : QObject(owner), window_(window), settings_(settings), prefix_(group + id + '/'),
          defaultSize_(window->size())
    {
        timer_.setSingleShot(true);
        timer_.setInterval(350);
        connect(&timer_, &QTimer::timeout, this, [this] { flush(); });

        viewMode_ = window->findChild<QComboBox *>("bookmarkViewMode");
        if (viewMode_ && viewMode_->window() != window) viewMode_ = nullptr;
        if (viewMode_) {
            viewMode_->setCurrentIndex(qBound(0, settings_.value(prefix_ + "viewMode", 0).toInt(),
                                              viewMode_->count()-1));
            connect(viewMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
                if (!applying_) settings_.setValue(prefix_ + "viewMode", index);
            });
        }
        bindParts();
        if (!parts_.isEmpty()) {
            auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F10")), window);
#ifdef Q_OS_MACOS
            shortcut->setKeys({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F10),
                               QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_F10)});
#endif
            connect(shortcut, &QShortcut::activated, this, [this] { arrange(); });
        }
        window->installEventFilter(this);
        const auto saved = settings_.value(prefix_ + "geometry").toMap();
        // Additional manuscripts must receive fresh placement from the
        // compositor. In particular, replaying a sibling's last tile sends
        // X11 a user-position hint and can make KWin vertically maximize it.
        // Older snapshots did not distinguish floating windows from tiles.
        if (!manuscriptWindow(window) || (!anotherManuscriptIsVisible(window) &&
            saved.value("floating").toBool()))
            applyGeometry(window, saved);
        lastGeometry_ = geometryOf(window);
    }

    void releaseParts()
    {
        flush();
        // The modeless arranger stores pointers to the active panel and column
        // controls. Close it before those controls go back to their document.
        if (auto *dialog = window_->findChild<QDialog *>("layoutEditor", Qt::FindDirectChildrenOnly))
            delete dialog;
        parts_.clear();
        delete partConnections_;
        partConnections_ = nullptr;
    }

    void bindParts()
    {
        if (partConnections_) return;
        QWidget *window = window_;
        partConnections_ = new QObject(this);
        for (auto *split : window->findChildren<QSplitter *>()) {
            if (!belongsToLayout(split, window) || split->objectName().isEmpty()) continue;
            // Qt file dialogs also contain implementation-private splitters.
            bool named = split->count() > 1 && !split->objectName().startsWith("qt_");
            for (int i = 0; i < split->count(); ++i)
                named &= !split->widget(i)->objectName().isEmpty() && !split->widget(i)->accessibleName().isEmpty();
            if (named) parts_.append(panelLayout(split, partConnections_));
        }
        for (auto *view : window->findChildren<QAbstractItemView *>()) {
            if (!belongsToLayout(view, window) || view->objectName().isEmpty() || view->objectName().startsWith("qt_"))
                continue;
            QHeaderView *header = nullptr;
            if (auto *tree = qobject_cast<QTreeView *>(view)) header = tree->header();
            else if (auto *table = qobject_cast<QTableView *>(view)) header = table->horizontalHeader();
            if (!header || header->count() < 2 || (header->isHidden() && view->objectName() != "genreList"))
                continue;
            parts_.append(columnLayout(view, header, partConnections_));
            header->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(header, &QWidget::customContextMenuRequested, partConnections_, [this, header](const QPoint &point) {
                auto *menu = new QMenu(header);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                auto *open = menu->addAction(tr("Arrange Panels and Columns…"));
                connect(open, &QAction::triggered, this, [this] { arrange(); });
                menu->popup(header->mapToGlobal(point));
            });
        }
        for (auto *part : parts_) {
            part->changed = [this, part] {
                if (!applying_ && window_ && window_->isVisible()) {
                    dirtyParts_.insert(part);
                    timer_.start();
                }
            };
        }
        restoreParts();
    }

    void openAgain()
    {
        if (!previouslyHidden_) return;
        previouslyHidden_ = false;
        restoreParts();
        // Retained tools (for example the bookmark manager) share preferences
        // with windows of the same type opened by a second manuscript.
        if (viewMode_) viewMode_->setCurrentIndex(settings_.value(prefix_ + "viewMode", 0).toInt());
        if (!manuscriptWindow(window_))
            applyGeometry(window_, settings_.value(prefix_ + "geometry").toMap());
        lastGeometry_ = geometryOf(window_);
    }
    void arrange() { if (window_) showLayoutEditor(window_, parts_); }
    void reset()
    {
        if (!window_) return;
        timer_.stop();
        dirtyParts_.clear();
        geometryDirty_ = false;
        applying_ = true;
        if (viewMode_) viewMode_->setCurrentIndex(0);
        for (auto *p : parts_) p->reset();
        for (auto *button : window_->findChildren<QToolButton *>("genreOrder"))
            if (button->window() == window_) button->setChecked(button->property("layoutDefaultOrder").toBool());
        if (window_->objectName() != "manuscriptWindow" && defaultSize_.isValid()) window_->resize(defaultSize_);
        if (auto *arranger = window_->findChild<QDialog *>("layoutEditor", Qt::FindDirectChildrenOnly))
            arranger->close();
        lastGeometry_ = geometryOf(window_);
        applying_ = false;
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != window_ || applying_) return false;
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
            if (window_->isVisible() && !(window_->windowState() & (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen))) {
                geometryDirty_ = true;
                timer_.start();
            }
            break;
        case QEvent::WindowDeactivate:
            flush();
            break;
        case QEvent::Hide:
            previouslyHidden_ = true;
            flush();
            break;
        default: break;
        }
        return false;
    }
private:
    void restoreParts()
    {
        applying_ = true;
        for (auto *p : parts_) {
            const auto state = settings_.value(prefix_ + p->key).toMap();
            if (!state.isEmpty()) p->restore(state);
        }
        applying_ = false;
    }
    void flush()
    {
        timer_.stop();
        if (!window_) return;
        for (auto *part : dirtyParts_) settings_.setValue(prefix_ + part->key, part->save());
        dirtyParts_.clear();
        auto geometry = geometryOf(window_);
        if (geometryDirty_ && geometry != lastGeometry_) {
            lastGeometry_ = geometry;
            // Retain the last genuine floating layout when the compositor
            // tiles a window. The next cold launch can still use that layout.
            if (manuscriptWindow(window_)) {
                if (!hasFloatingBounds(window_)) {
                    geometryDirty_ = false;
                    return;
                }
                geometry.insert("floating", true);
            }
            settings_.setValue(prefix_ + "geometry", geometry);
        }
        geometryDirty_ = false;
    }
    QPointer<QWidget> window_;
    QSettings &settings_;
    const QString prefix_;
    const QSize defaultSize_;
    QTimer timer_;
    QPointer<QComboBox> viewMode_;
    QObject *partConnections_ = nullptr;
    QList<LayoutPart *> parts_;
    QSet<LayoutPart *> dirtyParts_;
    QVariantMap lastGeometry_;
    bool applying_ = false, geometryDirty_ = false, previouslyHidden_ = false;
};

WindowLayouts::WindowLayouts(QWidget *manuscript, QSettings &settings)
    : QObject(manuscript), manuscript_(manuscript), settings_(settings)
{
    controllers().append(this);
    qApp->installEventFilter(this);
    // Apply the main window's preferred size before it becomes a native
    // window; the compositor has the final say on placement.
    track(manuscript, manuscript->objectName());
}

WindowLayouts::~WindowLayouts()
{
    qApp->removeEventFilter(this);
    controllers().removeAll(this);
}

void WindowLayouts::track(QWidget *window, const QString &name)
{
    if (auto *existing = windows_.value(window)) {
        existing->openAgain();
        return;
    }
    auto *state = new WindowLayoutState(window, name, settings_, this);
    windows_.insert(window, state);
    connect(window, &QObject::destroyed, this, [this, window] { delete windows_.take(window); });
}

bool WindowLayouts::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() != QEvent::Show || event->spontaneous()) return false;
    auto *window = qobject_cast<QWidget *>(object);
    if (!window || !window->isWindow()) return false;
    // QWidget::isAncestorOf stops at a window boundary; these tools have
    // their own native windows but still belong to this manuscript.
    for (QWidget *owner = window; owner; owner = owner->parentWidget()) {
        if (owner != manuscript_) continue;
        const QString name = window == manuscript_ ? window->objectName() : dialogKey(window);
        if (!name.isEmpty()) track(window, name);
        break;
    }
    return false;
}

void WindowLayouts::releaseManuscriptPanels()
{
    if (auto *state = windows_.value(manuscript_)) state->releaseParts();
}

void WindowLayouts::bindManuscriptPanels()
{
    if (auto *state = windows_.value(manuscript_)) state->bindParts();
}

void WindowLayouts::transferDocumentTools(QWidget *editor, WindowLayouts *destination)
{
    // Retained modeless windows move with the document. Transfer their live
    // layout observers and shortcuts instead of adding another set on Show.
    for (auto *tool : windows_.keys()) {
        for (auto *ancestor = tool->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (ancestor != editor) continue;
            auto *state = windows_.take(tool);
            state->setParent(destination);
            destination->windows_.insert(tool, state);
            connect(tool, &QObject::destroyed, destination, [destination, tool] {
                delete destination->windows_.take(tool);
            });
            break;
        }
    }
}

void WindowLayouts::arrange(QWidget *window)
{
    if (auto *state = windows_.value(window)) state->arrange();
}

void WindowLayouts::restoreDefaults()
{
    settings_.remove("layouts");
    settings_.remove("editor/title-sort-document-order");
    settings_.remove("editor/genre-sort-by-number");
    for (auto *controller : controllers()) {
        if (controller->settings_.fileName() != settings_.fileName()) continue;
        const auto states = controller->windows_.values();
        for (auto *state : states) state->reset();
    }
    settings_.sync();
}
