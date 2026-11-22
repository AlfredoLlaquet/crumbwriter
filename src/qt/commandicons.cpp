#include "commandicons.h"

#include <QApplication>
#include <QIconEngine>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPolygonF>
#include <QPointer>
#include <QStyleOptionToolButton>
#include <QToolButton>

namespace CommandIcons {
namespace {

// Styles request pixmaps at their own device scale. Read the owning widget's
// ink each time so changing the manuscript theme also changes existing icons.
class CommandIconEngine final : public QIconEngine {
public:
    CommandIconEngine(Symbol symbol, QWidget *owner) : symbol_(symbol), owner_(owner) {}

    QIconEngine *clone() const override { return new CommandIconEngine(symbol_, owner_); }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, result.rect(), mode, state);
        return result;
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto palette = owner_ ? owner_->palette() : QApplication::palette();
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : palette.currentColorGroup();
        const auto role = mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        const auto ink = palette.color(group, role);
        const qreal size = qMin(rect.width(), rect.height());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - size) / 2, rect.y() + (rect.height() - size) / 2);
        painter->scale(size / 18, size / 18);
        painter->setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        switch (symbol_) {
        case Symbol::Open:
            painter->drawPolyline(QPolygonF{{1.5, 14.5}, {1.5, 3.5}, {6.5, 3.5}, {8.5, 5.5}, {14, 5.5}, {14, 8}});
            painter->drawPolygon(QPolygonF{{1.5, 14.5}, {4.5, 8}, {16.5, 8}, {13.5, 14.5}});
            break;
        case Symbol::Add:
            painter->drawLine(QLineF(9, 2.5, 9, 15.5));
            painter->drawLine(QLineF(2.5, 9, 15.5, 9));
            break;
        case Symbol::Save:
            painter->drawPolygon(QPolygonF{{2.5, 2.5}, {12.5, 2.5}, {15.5, 5.5}, {15.5, 15.5}, {2.5, 15.5}});
            painter->drawPolyline(QPolygonF{{5.5, 2.5}, {5.5, 7}, {11, 7}, {11, 2.5}});
            painter->drawPolyline(QPolygonF{{5.5, 15.5}, {5.5, 10.5}, {12.5, 10.5}, {12.5, 15.5}});
            break;
        case Symbol::Find:
            painter->drawEllipse(QRectF(2, 2, 10, 10));
            painter->drawLine(QLineF(10.7, 10.7, 15.7, 15.7));
            break;
        case Symbol::Menu:
            painter->setPen(Qt::NoPen);
            painter->setBrush(ink);
            for (int top : {3, 8, 13})
                painter->drawRoundedRect(QRectF(1, top, 16, 2), 1, 1);
            break;
        }
        painter->restore();
    }

private:
    Symbol symbol_;
    QPointer<QWidget> owner_;
};

class MenuButton final : public QToolButton {
public:
    explicit MenuButton(QWidget *parent) : QToolButton(parent)
    {
        setIcon(CommandIcons::icon(Symbol::Menu, this));
        setIconSize(QSize(18, 18));
        setMinimumSize(28, 28);
        setAutoRaise(true);
        setFocusPolicy(Qt::StrongFocus);
        setPopupMode(QToolButton::InstantPopup);
    }

protected:
    void initStyleOption(QStyleOptionToolButton *option) const override
    {
        QToolButton::initStyleOption(option);
        // The whole surface opens one menu. The three bars already convey that.
        option->features.setFlag(QStyleOptionToolButton::HasMenu, false);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        const auto key = event->key();
        if (menu() && !(event->modifiers() & ~Qt::KeypadModifier) &&
            (key == Qt::Key_Space || key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Down)) {
            if (!event->isAutoRepeat()) {
                menu()->popup(mapToGlobal(QPoint(0, height())));
                if (!menu()->actions().isEmpty()) menu()->setActiveAction(menu()->actions().first());
            }
            event->accept();
            return;
        }
        QToolButton::keyPressEvent(event);
    }
};
} // namespace

QIcon icon(Symbol symbol, QWidget *owner)
{
    return QIcon(new CommandIconEngine(symbol, owner));
}

QToolButton *menuButton(QWidget *parent, const QString &accessibleName, const QString &toolTip)
{
    auto *button = new MenuButton(parent);
    button->setAccessibleName(accessibleName);
    button->setToolTip(toolTip);
    return button;
}

} // namespace CommandIcons
