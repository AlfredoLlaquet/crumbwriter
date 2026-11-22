#pragma once

#include <QIcon>

class QFileDialog;
class QLineEdit;
class QToolButton;
class QWidget;

namespace CommandIcons {

enum class Symbol { Open, Add, Save, Find, Menu, Back, Forward, ParentDirectory, NewFolder, ListView, DetailsView, ClearSearch };

QIcon icon(Symbol symbol, QWidget *owner);
void addSearchClearButton(QLineEdit *field);
QToolButton *menuButton(QWidget *parent, const QString &accessibleName, const QString &toolTip);
void setFileDialogIcons(QFileDialog *dialog);

} // namespace CommandIcons
