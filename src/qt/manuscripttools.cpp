#include "mainwindow.h"
#include "manuscriptsession.h"
#include "manuscriptoptions.h"
#include <QTabWidget>
#include "bookmarks.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"
#include "preferences.h"
#include "navigation.h"
#include "spellcheck.h"
#include "statisticsdialog.h"
#include "symbolpicker.h"
#include "synonympanel.h"
#include "synonymwriting.h"

#include <QLabel>
#include <QSplitter>

void ManuscriptSession::connectWritingTools()
{
    bookmarks_->onChanged = [this] { changed(); };
    spelling_->onChanged = [this] { changed(); };
    spelling_->onNotice = [this](const QString &message) { showNotice(message); };
    side_->genreOccurrences = [this](const QString &genre) { return synonyms_->showGenreOccurrences(genre); };
    editor_->model().observe(synonyms_, [this](const CrumbChange &change) {
        if (!change.genresChanged && !change.modeChanged) return;
        QStringList genres;
        if (!editor_->model().isGeneric()) {
            for (const auto &genre : editor_->model().genres()) genres.append(genre.name);
        }
        synonyms_->setGenres(genres);
    });
    synonyms_->manuscriptSelection = [this] {
        return ManuscriptText::selectedText(editor_->textCursor());
    };
    synonyms_->onChanged = [this] {
        spelling_->setAliases(synonyms_->allAliases());
        synonymWriting_->listsChanged();
        changed();
    };
    synonymWriting_->jumpTo = [this](int position) { jump(position); };
    synonymWriting_->onNotice = [this](const QString &message) { showNavigationStatus(message); };
    synonymWriting_->onHint = [this](const QString &message) {
        synonymHint_ = message;
        if (!editor_->viSelectionActive() && !webLookupPending_)
            setStatusMessage(message.isEmpty() ? store_->status() : message);
    };
    synonyms_->insertSynonym = [this](const QString &text) {
        editor_->setFocus();
        editor_->insertLiteral(text);
    };
    synonyms_->returnToEditor = [this] { editor_->setFocus(); };
    synonyms_->jumpToMatch = [this](int start, int end) {
        const auto original = editor_->textCursor();
        if (start != original.selectionStart() || end != original.selectionEnd())
            trail_.remember(original);
        editor_->selectMatch(start, end);
        editor_->setFocus();
    };
}

void ManuscriptSession::openSymbols()
{
    if (!symbols_) symbols_ = new SymbolPicker(editor_, prefs_.settings(), editor_);
    symbols_->openPicker();
}

bool ManuscriptSession::companionsPending() const
{
    return options_->pending() || bookmarks_->hasPending() || synonyms_->hasPending() || spelling_->hasPending();
}

bool ManuscriptSession::updateCompanions(CompanionAction action, const QString &manuscript)
{
    QStringList errors;
    auto update = [&](auto *companion) {
        QString error;
        bool ok = true;
        switch (action) {
        case CompanionAction::Open:   ok = companion->openManuscript(manuscript, &error); break;
        case CompanionAction::SaveAs: ok = companion->saveAs(manuscript, &error); break;
        case CompanionAction::Flush:
            if (companion->hasPending()) ok = companion->flush(&error);
            break;
        }
        if (!ok) errors.append(error.isEmpty() ? tr("Could not update a manuscript companion file.") : error);
    };
    // Relocated bookmarks describe the edited text. Publish those positions
    // after the matching manuscript save succeeds, including during live save.
    if (action != CompanionAction::Flush || !store_->isDirty()) update(bookmarks_);
    update(synonyms_);
    update(spelling_);
    if (action == CompanionAction::SaveAs) {
        QString error;
        configPromotionFailed_ = !options_->carryToPath(manuscript, &error);
        if (configPromotionFailed_ || !removeDraftConfig(&error)) errors.append(error);
    } else if (action == CompanionAction::Flush && options_->pending()) {
        QString error;
        if (!options_->flush(&error)) errors.append(error);
        else if (!path().isEmpty()) {
            configPromotionFailed_ = false;
            if (!removeDraftConfig(&error)) errors.append(error);
        }
    }
    if (!errors.isEmpty()) showNotice(errors.join(u'\n'));
    return errors.isEmpty();
}

void MainWindow::focusSynonyms(bool recurring)
{
    showPanel(leftPane_, true, synonymWidth_);
    if (recurring) active_->synonyms_->focusRecurring();
    else active_->synonyms_->focusCharacters();
}

void MainWindow::showPanel(QWidget *panel, bool show, int &preferredWidth)
{
    if (show == !panel->isHidden()) return;
    if (!show) {
        preferredWidth = qMax(1, panel->width());
        panel->setProperty("layoutRememberedSize", preferredWidth);
        panel->hide();
        editor()->setFocus();
        return;
    }
    auto sizes = split_->sizes();
    const int index = split_->indexOf(panel);
    const int editorIndex = split_->indexOf(tabs_);
    const int savedWidth = panel->property("layoutRememberedSize").toInt();
    const int width = qMin(savedWidth > 0 ? savedWidth : preferredWidth, qMax(1, sizes[editorIndex] / 2));
    sizes[index] = width;
    sizes[editorIndex] = qMax(1, sizes[editorIndex] - width);
    panel->show();
    split_->setSizes(sizes);
}

void MainWindow::showSidebar(bool show)
{
    showPanel(rightPane_, show, sidebarWidth_);
}

void ManuscriptSession::showStatistics()
{
    if (!statistics_) statistics_ = new StatisticsDialog(editor_->model(), editor_);
    statistics_->show();
    statistics_->raise();
    statistics_->activateWindow();
}
