#ifndef CRUMBWRITER_NAVIGATION_H
#define CRUMBWRITER_NAVIGATION_H

#include <QHash>
#include <QTextCursor>
#include <QWidget>
#include <functional>

class QLabel;
class QSettings;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class ManuscriptEditor;
struct CrumbChange;

class JumpHistory {
public:
    void remember(QTextCursor cursor);
    QTextCursor move(const QTextCursor &from, bool forward);
    void clear();
private:
    QVector<QTextCursor> previous_;
    QVector<QTextCursor> next_;
};

class NavigationPanel final : public QWidget {
public:
    NavigationPanel(ManuscriptEditor *editor, QSettings *settings, QWidget *parent = nullptr);
    void reset();
    void updateCurrent();
    void focusHistory();
    void focusGenres();
    void toggleGenreOrder();
    void setTodayCount(int count);
    std::function<void(int)> jumpTo;
private:
    void updateModel(const CrumbChange &change);
    void updateVisit(QTreeWidgetItem *item);
    void updateGenres();
    void openVisit(QTreeWidgetItem *item);
    void continueGenre(QTreeWidgetItem *item);
    ManuscriptEditor *editor_;
    QLabel *number_;
    QLabel *title_;
    QLabel *words_;
    QLabel *today_;
    QToolButton *genreOrder_;
    QTreeWidget *visits_;
    QTreeWidget *genres_;
    QHash<int, QTreeWidgetItem *> visitedLines_;
};

#endif
