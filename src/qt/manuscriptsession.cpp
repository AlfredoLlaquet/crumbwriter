#include "manuscriptsession.h"
#include "mainwindow.h"
#include "bookmarks.h"
#include "commandicons.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "manuscriptlinks.h"
#include "manuscriptoptions.h"
#include "manuscripttext.h"
#include "preferences.h"
#include "searchpanel.h"
#include "spellcheck.h"
#include "synonympanel.h"
#include "synonymwriting.h"
#include "wordtools.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTimer>

ManuscriptSession::ManuscriptSession(MainWindow *host, Preferences &preferences, int untitledNumber)
    : QObject(host), host_(host), prefs_(preferences), untitledNumber_(untitledNumber),
      editor_(new ManuscriptEditor), parkedPanels_(new QWidget(editor_)),
      search_(new SearchPanel(editor_, &prefs_.settings(), parkedPanels_)),
      synonyms_(new SynonymPanel(editor_->document(), parkedPanels_)),
      side_(new NavigationPanel(editor_, &prefs_.settings(), parkedPanels_)),
      store_(new DocumentStore(this))
{
    editor_->setObjectName("manuscript");
    editor_->setAccessibleName(tr("Manuscript"));
    parkedPanels_->setObjectName("inactiveTabPanels");
    parkedPanels_->hide();
    options_ = new ManuscriptOptions(editor_, store_, this);
    options_->onChanged = [this] { changed(); };
    editor_->onViPrompt = [this](const QString &prompt) {
        setStatusMessage(prompt.isEmpty() ? store_->status() : prompt);
    };
    store_->setSnapshotProvider([this] { return editor_->manuscriptText(); },
                                [this] { return editor_->model().provisionalMarkerLine(); },
                                [this] { return editor_->literalGuillemets(); });
    store_->captureMetadata = [this] {
        bookmarks_->captureForSave();
        return editor_->model().dailySnapshot();
    };
    store_->onPublished = [this](const QString &path, const QByteArray &metadata) {
        const QString error = editor_->model().publishDailySnapshot(path, metadata);
        if (!error.isEmpty()) showNotice(error);
        if (store_->path().isEmpty() && path == store_->draftPath()) {
            configDraft_ = path;
            QString configError;
            if (!options_->carryToPath(path, &configError)) showNotice(configError);
        }
        if (path == store_->path()) {
            if (QFileInfo(path) == QFileInfo(pendingSavePath_)) pendingSavePath_.clear();
            QString bookmarkError;
            if (!bookmarks_->publishSave(&bookmarkError)) showNotice(bookmarkError);
        }
    };
    bookmarks_ = new Bookmarks(editor_,
        [this] { return editor_->textCursor().blockNumber() + 1 + editor_->virtualRows(); },
        editor_->document(),
        [this](int line) {
            const auto &model = editor_->model();
            const auto &record = model.record(line - 1);
            return model.isGeneric() ? record.text : record.title;
        },
        [this](int line) {
            const auto block = editor_->document()->findBlockByNumber(line - 1);
            if (block.isValid()) jump(block.position());
        });
    spelling_ = new SpellCheck(editor_, editor_);
    synonymWriting_ = new SynonymWriting(editor_, synonyms_->matcher());
    words_ = new WordTools(editor_);
    words_->onNotice = [this](const QString &message) { showNavigationStatus(message); };
    connectWritingTools();
    editor_->model().onDailyCountError = [this](const QString &error) { showNotice(error); };
    store_->onChanged = [this] { changed(); };
    store_->onError = [this](const QString &error) {
        pendingSavePath_.clear();
        showNotice(error);
        if (host_->closingSession_ == this) host_->cancelClose();
    };
    store_->onPathChanged = [this](const QString &, const QString &target) {
        pendingSavePath_.clear();
        if (!loading_) {
            const QString dailyError = editor_->model().saveDailyCountAs(target);
            if (!dailyError.isEmpty()) showNotice(dailyError);
            if (!updateCompanions(CompanionAction::SaveAs, target)) {
                store_->keepEditing();
                if (host_->closingSession_ == this) host_->cancelClose();
            }
        }
        if (host_->active_ == this) prefs_.remember(target);
        changed();
    };
    store_->onSaveAsNeeded = [this] { chooseFile(true); };
    store_->onReadyToClose = [this] { host_->closeReady(this); };
    side_->jumpTo = [this](int pos) { jump(pos); };
    search_->recordOrigin = [this](const QTextCursor &c) { trail_.remember(c); };
    editor_->model().observe(this, [this](const CrumbChange &change) {
        if (change.modeChanged && host_->active_ == this) host_->updateModeControls();
        if (!loading_ && (change.textChanged || change.markerConfirmed)) {
            store_->markEdited();
            bookmarks_->refresh();
            if (host_->active_ == this && host_->isActiveWindow()) prefs_.remember(store_->path());
        }
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (!loading_ && host_->active_ == this && host_->isActiveWindow()) prefs_.remember(store_->path());
    });

    applyPreferences(host_->palette());
    changed();
}

ManuscriptSession::~ManuscriptSession()
{
    editor_->onViPrompt = {};
    editor_->model().onDailyCountError = {};
    store_->onChanged = {};
    store_->onError = {};
    store_->onSaveAsNeeded = {};
    store_->onReadyToClose = {};
    store_->onPathChanged = {};
    store_->captureMetadata = {};
    store_->onPublished = {};
    bookmarks_->onChanged = {};
    synonyms_->onChanged = {};
    delete synonymWriting_; // Disconnect from the matcher while both panels are alive.
    delete spelling_; // Its viewport callbacks belong to the still-live editor.
    // A search or occurrence worker may still reference the document during
    // its teardown. Destroy document clients before deleting their editor.
    delete search_;
    delete side_;
    delete synonyms_;
    delete bookmarks_;
    delete words_;
    delete options_;
    delete editor_;
}

bool ManuscriptSession::load(const QString &name, bool recovery)
{
    loading_ = true;
    QString text, error;
    const bool ok = recovery ? store_->openDraft(name, &text, &error)
                             : store_->open(name, prefs_.enabled("backup"), &text, &error);
    if (ok) {
        showNotice({});
        trail_.clear();
        search_->reset();
        words_->closeThesaurus();
        webLookupPending_ = false;
        configDraft_ = recovery ? name : QString();
        QString configError;
        if (!options_->resetForPath(recovery ? name : store_->path(), &configError)) showNotice(configError);
        editor_->loadText(text);
        const QString dailyError = editor_->model().openDailyCount(recovery ? name : store_->path());
        if (!dailyError.isEmpty()) showNotice(dailyError);
        side_->reset();
        updateCompanions(CompanionAction::Open, store_->path());
        if (recovery) showNotice("Recovered an unsaved manuscript. Use Save As to give it a filename.");
        prefs_.remember(store_->path());
    } else {
        showNotice(error);
    }
    loading_ = false;
    changed();
    return ok;
}

QString ManuscriptSession::path() const { return store_->path(); }

void ManuscriptSession::applyPreferences(const QPalette &p)
{
    editor_->setPaperPalette(p);
    editor_->setPaperActive(host_->isActiveWindow());
    const QFont font = prefs_.manuscriptFont();
    if (editor_->font() != font)
        editor_->setFont(font);
    editor_->setSmartPunctuation(prefs_.enabled("smart-punctuation"));
    editor_->setCopyDeletedText(prefs_.enabled("copy-deleted"));
    synonymWriting_->applySettings(prefs_.synonymSettings());
    spelling_->setEnabled(prefs_.enabled("spell-check"));
    store_->configureLiveSave(prefs_.enabled("live-save"), prefs_.saveDelay());
    side_->updateCurrent();
}

QString ManuscriptSession::label() const
{
    if (!path().isEmpty()) return QFileInfo(path()).fileName();
    return untitledNumber_ == 1 ? tr("Untitled") : tr("Untitled %1").arg(untitledNumber_);
}

bool ManuscriptSession::needsConfirmation() const
{
    return store_->needsCloseConfirmation() || companionsPending();
}

bool ManuscriptSession::removeDraftConfig(QString *error)
{
    if (configDraft_.isEmpty()) return true;
    const QString sidecar = configDraft_ + QStringLiteral(".config");
    if (QFileInfo::exists(sidecar) && !QFile::remove(sidecar)) {
        *error = tr("Could not remove the private recovery settings file %1.").arg(sidecar);
        return false;
    }
    configDraft_.clear();
    return true;
}

void ManuscriptSession::changed()
{
    if (!editor_->viSelectionActive() && !webLookupPending_)
        statusMessage_ = synonymHint_.isEmpty() ? store_->status() : synonymHint_;
    host_->sessionChanged(this);
}

void ManuscriptSession::setStatusMessage(const QString &message)
{
    statusMessage_ = message;
    if (host_->active_ == this) host_->updateFooter();
}

void ManuscriptSession::showNotice(const QString &message)
{
    noticeMessage_ = message;
    if (host_->active_ == this) host_->updateFooter();
}

void ManuscriptSession::showNavigationStatus(const QString &message)
{
    setStatusMessage(message);
    QTimer::singleShot(2500, this, [this, message] {
        if (statusMessage_ == message && !editor_->viSelectionActive() && !webLookupPending_) changed();
    });
}

void ManuscriptSession::chooseFile(bool saving)
{
    if (chooser_) {
        if (saving && chooser_->acceptMode() != QFileDialog::AcceptSave) {
            chooser_->close();
            chooser_.clear();
        } else {
            chooser_->raise();
            chooser_->activateWindow();
            return;
        }
    }
    auto *dialog = new QFileDialog(editor_, saving ? "Save Manuscript As" : "Open Manuscript");
    chooser_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setAcceptMode(saving ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog->setFileMode(saving ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog->setNameFilters({"Text files (*.txt)", "All files (*)"});
    if (!path().isEmpty()) dialog->setDirectory(QFileInfo(path()).absolutePath());
    if (saving) dialog->selectFile(path().isEmpty()
        ? (editor_->model().isGeneric() ? "Manuscript.txt" : "Crumbs.txt")
        : QFileInfo(path()).fileName());
    connect(dialog, &QFileDialog::fileSelected, this, [this, saving](const QString &name) {
        if (!saving) { host_->openDocumentInTab(name); return; }
        if (!host_->canWritePath(this, name)) {
            store_->keepEditing();
            if (host_->closingSession_ == this) host_->cancelClose();
            showNotice("That manuscript is already open in another tab or window.");
            return;
        }
        editor_->finishTyping();
        pendingSavePath_ = name;
        store_->saveAs(name, prefs_.enabled("backup"));
    });
    if (saving) connect(dialog, &QDialog::rejected, this, [this] {
        store_->keepEditing();
        if (host_->closingSession_ == this) host_->cancelClose();
    });
    dialog->show();
    CommandIcons::setFileDialogIcons(dialog);
}

void ManuscriptSession::save(bool asNew)
{
    if (!pendingSavePath_.isEmpty()) {
        showNotice(tr("Wait for the current Save As operation to finish."));
        return;
    }
    editor_->finishTyping();
    if (asNew || path().isEmpty()) { chooseFile(true); return; }
    updateCompanions(CompanionAction::Flush);
    store_->save();
}

void ManuscriptSession::jump(int position)
{
    if (position < 0) return;
    if (position != editor_->textCursor().position()) trail_.remember(editor_->textCursor());
    editor_->jumpTo(position);
    editor_->setFocus();
}

void ManuscriptSession::appendCrumb()
{
    editor_->appendCrumb();
    if (!search_->isHidden()) search_->closeSearch(false);
    editor_->setFocus();
}

void ManuscriptSession::goBack(bool forward)
{
    auto target = trail_.move(editor_->textCursor(), forward);
    if (!target.isNull()) {
        editor_->jumpTo(target.position());
        editor_->setFocus();
    } else {
        showNotice(forward ? "No later jump." : "No earlier jump.");
    }
}

void ManuscriptSession::numberingWarning(bool backward)
{
    const auto caret = editor_->textCursor();
    auto warn = editor_->model().nextWarning(caret.selectionStart(), backward);
    if (!warn) { showNotice("No numbering warnings in this manuscript."); return; }
    const auto block = editor_->document()->findBlockByNumber(warn->line);
    if (!block.isValid()) return;
    trail_.remember(caret);
    editor_->selectRange(block.position() + warn->end, block.position() + warn->start);
    editor_->setFocus();
    const QString subject = warn->genre.isEmpty() ? "Crumb number" : warn->genre + " genre number";
    showNotice(QString("%1 on line %2. Expected %3, found %4.")
               .arg(subject).arg(warn->line + 1).arg(warn->expected, warn->actual));
}

bool ManuscriptSession::followAt(int position, bool backward, bool mouse)
{
    if (position < 0) return false;
    QTextCursor c(editor_->document());
    c.setPosition(position);
    const int here = c.positionInBlock();
    const QString line = ManuscriptText::blockText(c.block());
    if (mouse || !editor_->virtualRows()) {
        const auto link = ManuscriptLinks::at(line, here, !mouse);
        if (!link.isEmpty()) {
            ManuscriptLinks::open(link, editor_);
            return true;
        }
    }
    const auto &record = editor_->model().record(c.blockNumber());
    if (editor_->model().isGeneric() || (!mouse && editor_->virtualRows())) {
        if ((mouse || !editor_->virtualRows()) && synonymWriting_->follow(position, backward, mouse)) return true;
        if (!mouse) showNavigationStatus("Place the caret on a web link or a name from Characters or Recurring.");
        return false;
    }
    QString labels = "crumb";
    if (!record.label.isEmpty() && record.label.compare("crumb", Qt::CaseInsensitive) != 0)
        labels += "|" + QRegularExpression::escape(record.label);
    const QRegularExpression expression("(?<![\\p{L}\\p{M}\\p{N}_])(?:" + labels +
        ")\\h+([0-9]+)(?![\\p{L}\\p{M}\\p{N}_])",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    auto matches = expression.globalMatch(line);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (here < match.capturedStart() || here > match.capturedEnd() ||
            (mouse && here == match.capturedEnd())) continue;
        bool ok;
        const quint64 number = match.captured(1).toULongLong(&ok);
        const int dest = ok ? editor_->model().findNumber(number, c.position(), false, true) : -1;
        if (dest >= 0) jump(dest);
        else showNotice(QString("Crumb %1 was not found.").arg(match.captured(1)));
        return true;
    }
    if (synonymWriting_->follow(position, backward, mouse)) return true;
    if (!mouse) showNavigationStatus("Place the caret on a web link, a mention, or a reference such as “crumb 200”.");
    return false;
}
