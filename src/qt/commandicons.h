#pragma once

#include <QIcon>

class QFileDialog;
class QLineEdit;
class QToolButton;
class QWidget;

namespace CommandIcons {

enum class Symbol { Open, Add, Save, Find, Menu, Back, Forward, ParentDirectory, ClearSearch };

QIcon icon(Symbol symbol, QWidget *owner);
void addSearchClearButton(QLineEdit *field);
QToolButton *menuButton(QWidget *parent, const QString &accessibleName, const QString &toolTip);
void setFileNavigationIcons(QFileDialog *dialog);

} // namespace CommandIcons
