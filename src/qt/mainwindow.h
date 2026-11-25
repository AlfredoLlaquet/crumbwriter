#ifndef CRUMBWRITER_MAINWINDOW_H
#define CRUMBWRITER_MAINWINDOW_H

#include <QHash>
#include <QPointer>
#include <QWidget>
#include <functional>

class QAction;
class ManuscriptEditor;
class ManuscriptSession;
class Preferences;
class QDialog;
class QGridLayout;
class QLabel;
class QMenu;
class QMessageBox;
class QSplitter;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class WindowLayouts;

class MainWindow final : public QWidget {
    friend class ManuscriptSession;
public:
    explicit MainWindow(Preferences &preferences, QWidget *parent = nullptr);
    ~MainWindow() override;
    // Replace the active document only when its store permits it (reload/test
    // entry point). Normal file opens always use openDocumentInTab().
    bool loadDocument(const QString &path, bool recovery = false);
    bool openDocumentInTab(const QString &path, bool recovery = false);
    void newTab();
    void closeCurrentTab();
    void reopenClosedTab();
    void moveTabToNewWindow();
    int tabCount() const;
    QString path() const;
    void activateManuscript(bool goToEnd);
    void applyPreferences();
    void showNotice(const QString &message);
    ManuscriptEditor *editor() const;
    std::function<MainWindow *()> makeWindow;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void buildToolbar(QVBoxLayout *layout);
    void buildMenu();
    void updateModeControls();
    void focusNavigation(bool titlesOrGenres);
    void focusSynonyms(bool recurring);
    void showPanel(QWidget *panel, bool show, int &preferredWidth);
    void showSidebar(bool show);
    void fitFooter();
    void openCrumbsWebsite();
    void showAbout();
    MainWindow *newWindow();
    void addSession(ManuscriptSession *session);
    void removeSession(ManuscriptSession *session);
    void currentTabChanged();
    void stepTab(int direction);
    void sessionChanged(ManuscriptSession *session);
    void updateFooter();
    void requestClose(const QList<ManuscriptSession *> &sessions, bool wholeWindow);
    void showCloseDialog();
    void refreshCloseDialog();
    void continueClose();
    void closeReady(ManuscriptSession *session);
    void cancelClose();
    void rememberClosed(ManuscriptSession *session);
    bool canWritePath(const ManuscriptSession *writer, const QString &path) const;
    static ManuscriptSession *findOpen(const QString &path);
    void selectSession(ManuscriptSession *session, bool focus = true);
    ManuscriptSession *sessionForDialog(QWidget *dialog) const;

    struct ClosedTab { QString path; int anchor, position, scroll; };
    Preferences &prefs_;
    QTabWidget *tabs_ = nullptr;
    QList<ManuscriptSession *> sessions_;
    ManuscriptSession *active_ = nullptr;
    QList<ClosedTab> closedTabs_;
    WindowLayouts *layouts_ = nullptr;
    QSplitter *split_ = nullptr;
    QWidget *leftPane_, *rightPane_;
    QVBoxLayout *searchLayout_, *leftLayout_, *rightLayout_;
    QLabel *status_ = nullptr;
    QToolButton *website_ = nullptr;
    QGridLayout *footer_ = nullptr;
    QLabel *mode_ = nullptr;
    QLabel *notice_ = nullptr;
    QWidget *noticeBar_ = nullptr;
    QMenu *menu_ = nullptr;
    QToolButton *menuButton_ = nullptr;
    QAction *appendTool_ = nullptr, *appendAction_ = nullptr, *copyAction_ = nullptr;
    QAction *deleteAction_ = nullptr, *goToNumberAction_ = nullptr, *indexAction_ = nullptr, *sortAction_ = nullptr;
    QAction *reopenAction_ = nullptr, *markupAction_ = nullptr;
    QList<QAction *> crumbOnlyActions_, genericOnlyActions_;
    QPointer<QMessageBox> closeDialog_;
    QPointer<QDialog> about_;
    QList<ManuscriptSession *> closeTargets_, closeQueue_;
    quint64 closeAttempt_ = 0;
    QHash<ManuscriptSession *, quint64> discardRevisions_;
    ManuscriptSession *closingSession_ = nullptr;
    bool closingWindow_ = false, discarding_ = false, allowClose_ = false;
    int sidebarWidth_ = 230, synonymWidth_ = 190;
};

#endif
