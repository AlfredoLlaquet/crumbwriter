#ifndef CRUMBWRITER_MAINWINDOW_H
#define CRUMBWRITER_MAINWINDOW_H

#include "navigation.h"
#include <QPointer>
#include <QWidget>
#include <functional>

class Bookmarks;
class DocumentStore;
class QFileDialog;
class QLabel;
class QMenu;
class QMessageBox;
class QSplitter;
class QToolButton;
class QVBoxLayout;
class Preferences;
class SearchPanel;

class MainWindow final : public QWidget {
public:
    explicit MainWindow(Preferences &preferences, QWidget *parent = nullptr);
    ~MainWindow() override;
    bool loadDocument(const QString &path, bool recovery = false);
    QString path() const;
    void activateManuscript(bool goToEnd);
    void applyPreferences();
    void showNotice(const QString &message);
    ManuscriptEditor *editor() const { return editor_; }

    std::function<void(const QString &)> openRequested;
    std::function<void()> newRequested;
    std::function<bool(const QString &)> pathAvailable;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void buildToolbar(QVBoxLayout *layout);
    void buildMenu();
    void chooseFile(bool saving);
    void documentStateChanged();
    void showCloseDialog();
    void save(bool asNew);
    void jump(int position);
    void goBack(bool forward);
    void numberingWarning(bool backward);
    void followReference();
    bool flushBookmarks();
    void showSidebar(bool show);

    Preferences &prefs_;
    ManuscriptEditor *editor_ = nullptr;
    NavigationPanel *side_ = nullptr;
    SearchPanel *search_ = nullptr;
    Bookmarks *bookmarks_ = nullptr;
    DocumentStore *store_ = nullptr;
    QSplitter *split_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *notice_ = nullptr;
    QWidget *noticeBar_ = nullptr;
    QMenu *menu_ = nullptr;
    QToolButton *menuButton_ = nullptr;
    QPointer<QFileDialog> chooser_;
    QPointer<QMessageBox> closeDialog_;
    JumpHistory trail_;
    bool loading_ = false;
    bool allowClose_ = false;
    bool discarding_ = false;
    int sidebarWidth_ = 230;
};

#endif
