#pragma once

#include "navigation.h"

#include <QObject>
#include <QPointer>

class Bookmarks;
class DocumentStore;
class MainWindow;
class ManuscriptOptions;
class Preferences;
class QFileDialog;
class SearchPanel;
class SpellCheck;
class StatisticsDialog;
class SymbolPicker;
class SynonymPanel;
class SynonymWriting;
class WordTools;

// One live editor and one set of writing tools per open manuscript. Moving a
// tab changes its window, never its document, undo stack, workers or sidecars.
class ManuscriptSession final : public QObject {
    friend class MainWindow;
public:
    ManuscriptSession(MainWindow *host, Preferences &preferences, int untitledNumber);
    ~ManuscriptSession() override;
    QString path() const;
    QString label() const;
    bool load(const QString &path, bool recovery);

private:
    void connectWritingTools();
    void applyPreferences(const QPalette &palette);
    void changed();
    void setStatusMessage(const QString &message);
    void showNotice(const QString &message);
    void showNavigationStatus(const QString &message);
    void jump(int position);
    void goBack(bool forward);
    void appendCrumb();
    void numberingWarning(bool backward);
    bool followAt(int position, bool backward, bool mouse = false);
    void openSymbols();
    void showStatistics();
    void chooseFile(bool saving);
    void save(bool asNew);
    bool companionsPending() const;
    bool needsConfirmation() const;
    bool removeDraftConfig(QString *error);
    enum class CompanionAction { Open, SaveAs, Flush };
    bool updateCompanions(CompanionAction action, const QString &manuscript = {});

    MainWindow *host_;
    Preferences &prefs_;
    int untitledNumber_;
    ManuscriptEditor *editor_;
    QWidget *parkedPanels_;
    SearchPanel *search_;
    SynonymPanel *synonyms_;
    NavigationPanel *side_;
    DocumentStore *store_;
    Bookmarks *bookmarks_ = nullptr;
    SpellCheck *spelling_ = nullptr;
    SynonymWriting *synonymWriting_ = nullptr;
    WordTools *words_ = nullptr;
    ManuscriptOptions *options_ = nullptr;
    QPointer<QFileDialog> chooser_;
    QPointer<SymbolPicker> symbols_;
    QPointer<StatisticsDialog> statistics_;
    JumpHistory trail_;
    QString statusMessage_, noticeMessage_, synonymHint_, pendingSavePath_;
    QString configDraft_; // Private draft whose .config travels to the eventual named manuscript.
    bool configPromotionFailed_ = false;
    bool loading_ = false;
    bool webLookupPending_ = false;
    bool searchOpen_ = false;
    int openingCaret_ = -1;
};
