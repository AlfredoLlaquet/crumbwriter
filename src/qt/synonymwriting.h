#pragma once

#include "synonymmatch.h"
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <optional>

class QLabel;
class ManuscriptEditor;
class SynonymChoices;

enum class SynonymCompletion { Automatic, OnDemand, Off };
enum class SynonymUnderlining { All, CurrentGroup, Off };

struct SynonymWritingSettings {
    SynonymCompletion charactersCompletion = SynonymCompletion::Automatic;
    SynonymCompletion recurringCompletion = SynonymCompletion::Automatic;
    SynonymUnderlining charactersUnderlining = SynonymUnderlining::All;
    SynonymUnderlining recurringUnderlining = SynonymUnderlining::All;
    int minimumPrefix = 3;
    QColor underlineColor; // An invalid color follows the manuscript palette.
};

class SynonymWriting final : public QObject {
public:
    SynonymWriting(ManuscriptEditor *editor, SynonymMatcher *matcher);
    ~SynonymWriting() override;
    void applySettings(const SynonymWritingSettings &settings);
    void listsChanged();
    void complete();
    void dismissHint();
    void navigate(bool backward);
    bool follow(int position, bool backward, bool insideOnly = false);

    std::function<void(int)> jumpTo;
    std::function<void(const QString &)> onNotice;
    std::function<void(const QString &)> onHint;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void afterTyping();
    void placeHint();
    void setStatusHint(const QString &text);
    void accept(const SynonymSuggestion &suggestion);
    void visit(const SynonymMention &source, int group, bool backward);
    void refreshHighlights();
    void scheduleHighlights();
    void showChoices(const QString &title, const QStringList &labels, int position,
                     std::function<void(int)> chosen);

    ManuscriptEditor *editor_;
    QPointer<SynonymMatcher> matcher_;
    QPointer<QLabel> ghost_;
    QPointer<SynonymChoices> choices_;
    SynonymWritingSettings settings_;
    std::optional<SynonymSuggestion> suggestion_;
    QString statusHint_;
    QTimer highlights_;
    QList<QPair<int, int>> paintedRanges_;
    QList<int> paintedGroups_;
    QColor paintedColor_;
    bool highlightsDirty_ = true;
    bool preediting_ = false;
    int visibleFirst_ = -1, visibleLast_ = -1;
    int pinnedGroup_ = -1, pinnedPosition_ = -1;
};
