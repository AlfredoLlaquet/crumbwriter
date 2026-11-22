#pragma once

#include <QIcon>

class QToolButton;
class QWidget;

namespace CommandIcons {

enum class Symbol { Open, Add, Save, Find, Menu };

QIcon icon(Symbol symbol, QWidget *owner);
QToolButton *menuButton(QWidget *parent, const QString &accessibleName, const QString &toolTip);

} // namespace CommandIcons
