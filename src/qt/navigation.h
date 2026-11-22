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
class PanelSearch;
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
    std::function<void(int)> jumpTo;
protected:
    void changeEvent(QEvent *event) override;
private:
    void updateMode();
    void updateToday();
    void updateModel(const CrumbChange &change);
    bool isVisitLine(int row) const;
    void updateVisit(QTreeWidgetItem *item);
    void updateGenres();
    bool openVisit();
    bool openGenreOrTitle();
    ManuscriptEditor *editor_;
    QSettings *settings_;
    QLabel *number_;
    QLabel *title_;
    QLabel *words_;
    QLabel *today_;
    QLabel *genreHeading_;
    QToolButton *genreOrder_;
    QTreeWidget *visits_;
    QTreeWidget *genres_ = nullptr;
    PanelSearch *historySearch_;
    PanelSearch *genresSearch_;
    QHash<int, QTreeWidgetItem *> visitedLines_;
};

#endif
