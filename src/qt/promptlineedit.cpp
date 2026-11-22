#include "promptlineedit.h"
#include "editorcolors.h"

#include <QEvent>
#include <QScopedValueRollback>
#include <QTimer>
#include <cmath>

namespace {
double luminance(const QColor &ink)
{
    const auto linear = [](double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(ink.redF()) + 0.7152 * linear(ink.greenF()) + 0.0722 * linear(ink.blueF());
}

double contrast(const QColor &ink, double paper)
{
    const double value = luminance(ink);
    return (qMax(value, paper) + 0.05) / (qMin(value, paper) + 0.05);
}

QColor correctedHint(const QPalette &p, QPalette::ColorGroup group)
{
    const auto base = p.color(group, QPalette::Base);
    const auto paper = EditorColors::blend(p.color(group, QPalette::Window), base, base.alpha());
    const auto visible = [&](const QColor &ink) { return EditorColors::blend(paper, ink, ink.alpha()); };
    const auto hint = visible(p.color(group, QPalette::PlaceholderText));
    const double background = luminance(paper);
    if (contrast(hint, background) >= 4.5) return {}; // Keep a readable native brush, including its alpha.

    auto ink = visible(p.color(group, QPalette::Text));
    if (contrast(ink, background) < 4.5)
        ink = contrast(Qt::black, background) > contrast(Qt::white, background) ? Qt::black : Qt::white;
    // Allow for antialiasing on small native fonts. Preserve as much of the
    // original hue as possible and return opaque ink to avoid a second fade.
    const auto target = qMin(5.25, contrast(ink, background));
    int low = 0, high = 255;
    while (low < high) {
        const int middle = (low + high) / 2;
        if (contrast(EditorColors::blend(hint, ink, middle), background) < target) low = middle + 1;
        else high = middle;
    }
    return EditorColors::blend(hint, ink, low);
}
}

PromptLineEdit::PromptLineEdit(QWidget *parent) : QLineEdit(parent)
{
    watchParent();
    refreshPrompt();
}

void PromptLineEdit::watchParent()
{
    if (watchedParent_) watchedParent_->removeEventFilter(this);
    watchedParent_ = parentWidget();
    if (watchedParent_) watchedParent_->installEventFilter(this);
}

bool PromptLineEdit::event(QEvent *event)
{
    const auto type = event->type();
    const bool handled = QLineEdit::event(event);
    if (type == QEvent::ParentChange) watchParent();
    if (type == QEvent::ParentChange || type == QEvent::PaletteChange || type == QEvent::StyleChange || type == QEvent::Polish)
        queueRefresh();
    return handled;
}

bool PromptLineEdit::eventFilter(QObject *watched, QEvent *event)
{
    // A change solely to an inherited placeholder can be masked by our current
    // correction. Observe the parent as well, then let Qt finish propagation.
    if (watched == watchedParent_ && event->type() == QEvent::PaletteChange) queueRefresh();
    return QLineEdit::eventFilter(watched, event);
}

void PromptLineEdit::queueRefresh()
{
    if (refreshing_ || queued_) return;
    queued_ = true;
    QTimer::singleShot(0, this, [this] { queued_ = false; refreshPrompt(); });
}

void PromptLineEdit::refreshPrompt()
{
    QScopedValueRollback<bool> guard(refreshing_, true);
    constexpr auto role = QPalette::PlaceholderText;
    auto source = palette();
    bool restore = false;
    for (auto group : {QPalette::Active, QPalette::Inactive}) {
        if (!corrected_.isBrushSet(group, role) || !source.isBrushSet(group, role) ||
            source.brush(group, role) != corrected_.brush(group, role)) continue;
        // Release only our previous override. Keep genuine field-specific colors
        // and let Qt resolve the latest native palette and the whole parent chain.
        if (original_.isBrushSet(group, role)) source.setBrush(group, role, original_.brush(group, role));
        else {
            QPalette oneRole;
            oneRole.setColor(group, role, Qt::black);
            source.setResolveMask(source.resolveMask() & ~oneRole.resolveMask());
        }
        restore = true;
    }
    if (restore) setPalette(source);
    original_ = palette();
    corrected_ = QPalette();
    auto result = original_;
    for (auto group : {QPalette::Active, QPalette::Inactive}) {
        const QColor ink = correctedHint(original_, group);
        if (!ink.isValid()) continue;
        result.setColor(group, role, ink);
        corrected_.setColor(group, role, ink);
    }
    if (corrected_.resolveMask()) setPalette(result);
}
