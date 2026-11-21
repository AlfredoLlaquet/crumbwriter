#pragma once

#include "searchengine.h"
#include "searchhistory.h"
#include <QTextCursor>
#include <QWidget>
#include <optional>

class ManuscriptEditor;
class QCheckBox;
class QFrame;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QStackedWidget;
class QStringListModel;
class QTimer;
class QToolButton;

class SearchPanel : public QWidget {
public:
    SearchPanel(ManuscriptEditor *editor, QSettings *settings, QWidget *parent = nullptr);
    ~SearchPanel() override;

    void find(bool backwards = false);
    void findInMode(cwsearch::Mode mode);
    void findReplace();
    void goToNumber();
    void repeat(bool opposite = false);
    void closeSearch();
    void reset();
    bool handleKey(QKeyEvent *event);
    // A new intentional search can produce many live previews, but one return point.
    std::function<void(const QTextCursor &)> recordOrigin;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    using Mode = cwsearch::Mode;
    using Span = cwsearch::Span;

    void buildUi();
    void open(std::optional<Mode> requestedMode, bool backwards, bool replace);
    void setMode(Mode mode);
    void updateControls();
    QLineEdit *entry() const;
    void focusEntry();
    void beginOrigin();
    void saveOrigin(int destination);
    void remember();
    void showRecent();
    void recall(int row);

    const QString &snapshot();
    void invalidateIndex();
    void rebuild(bool preview);
    void collectMatches();
    void indexFinished(cwsearch::Matches result);
    void updateHighlights();
    void scheduleHighlights();
    Span literalAt(int position, bool reverse, bool &didWrap, bool &waitForIndex);
    void previewText(bool restorePrevious);
    void step(bool backwards);
    void stepNumber(bool backwards);
    void showMatch(Span hit, bool wrapped);
    void showStatus();
    void setNotice(const QString &text);
    void cancelPreview();
    void replaceOne();
    void replaceAll();
    void previewReady(cwsearch::ReplacementPlan plan);
    void applyPreview();

    ManuscriptEditor *editor;
    cwsearch::History history;
    cwsearch::Options options;
    Mode lastTextMode = Mode::Text;
    cwsearch::Pattern pattern;
    cwsearch::Matches matches;
    std::shared_ptr<std::atomic_bool> indexCancel;
    std::shared_ptr<std::atomic_bool> replacementCancel;
    std::optional<cwsearch::ReplacementPlan> pendingReplacement;
    QVector<bool> pendingSteps;
    QVector<Span> visibleHits;
    QString paper;
    QString notice;
    QTextCursor origin;
    Span hit;
    bool paperDirty = true;
    bool indexReady = false;
    bool previewPending = false;
    bool restorePending = false;
    bool backwards = false;
    bool showingReplace = false;
    bool recorded = false;
    bool movingCursor = false;
    bool settingControls = false;
    bool replacing = false;
    bool wrapped = false;

    QLabel *queryLabel;
    QToolButton *modes;
    QMenu *modeMenu;
    QStackedWidget *entries;
    QLineEdit *findEntry;
    QLineEdit *numberEntry;
    QPushButton *previous;
    QPushButton *next;
    QPushButton *recent;
    QCheckBox *matchCase;
    QCheckBox *wholeWords;
    QCheckBox *wrap;
    QLabel *status;
    QWidget *replaceRow;
    QLineEdit *replacement;
    QPushButton *replaceButton;
    QPushButton *allButton;
    QPlainTextEdit *preview;
    QPushButton *cancelButton;
    QFrame *recentPopup;
    QListView *recentList;
    QStringListModel *recentModel;
    QVector<cwsearch::HistoryEntry> recentEntries;
    QTimer *scanDelay;
    QTimer *refreshDelay;
    QTimer *highlightDelay;
};
