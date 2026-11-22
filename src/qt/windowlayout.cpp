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
        for (auto *split : window->findChildren<QSplitter *>()) {
            if (split->window() != window || split->objectName().isEmpty()) continue;
            // Qt file dialogs also contain implementation-private splitters.
            bool named = split->count() > 1 && !split->objectName().startsWith("qt_");
            for (int i = 0; i < split->count(); ++i)
                named &= !split->widget(i)->objectName().isEmpty() && !split->widget(i)->accessibleName().isEmpty();
            if (named) parts_.append(panelLayout(split, this));
        }
        for (auto *view : window->findChildren<QAbstractItemView *>()) {
            if (view->window() != window || view->objectName().isEmpty() || view->objectName().startsWith("qt_"))
                continue;
            QHeaderView *header = nullptr;
            if (auto *tree = qobject_cast<QTreeView *>(view)) header = tree->header();
            else if (auto *table = qobject_cast<QTableView *>(view)) header = table->horizontalHeader();
            if (!header || header->count() < 2 || (header->isHidden() && view->objectName() != "genreList"))
                continue;
            parts_.append(columnLayout(view, header, this));
            header->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(header, &QWidget::customContextMenuRequested, this, [this, header](const QPoint &point) {
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
        if (!parts_.isEmpty()) {
            auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F10")), window);
#ifdef Q_OS_MACOS
            shortcut->setKeys({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F10),
                               QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_F10)});
#endif
            connect(shortcut, &QShortcut::activated, this, [this] { arrange(); });
        }
        window->installEventFilter(this);
        applyGeometry(window, settings_.value(prefix_ + "geometry").toMap());
        restoreParts();
        lastGeometry_ = geometryOf(window);
    }

    void openAgain()
    {
        if (!previouslyHidden_) return;
        previouslyHidden_ = false;
        restoreParts();
        // Retained tools (for example the bookmark manager) share preferences
        // with windows of the same type opened by a second manuscript.
        if (viewMode_) viewMode_->setCurrentIndex(settings_.value(prefix_ + "viewMode", 0).toInt());
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
        if (auto *button = window_->findChild<QToolButton *>("genreOrder"); button && button->window() == window_)
            button->setChecked(button->property("layoutDefaultOrder").toBool());
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
        const auto geometry = geometryOf(window_);
        if (geometryDirty_ && geometry != lastGeometry_) {
            settings_.setValue(prefix_ + "geometry", geometry);
            lastGeometry_ = geometry;
        }
        geometryDirty_ = false;
    }
    QPointer<QWidget> window_;
    QSettings &settings_;
    const QString prefix_;
    const QSize defaultSize_;
    QTimer timer_;
    QPointer<QComboBox> viewMode_;
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
