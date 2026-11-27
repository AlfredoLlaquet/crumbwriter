#include "spellcheck.h"
#include "editorcolors.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"
#include "spellreview.h"
#include "spellwords.h"
#include "textcompanion.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <algorithm>

namespace {
const QString missingDictionary = QStringLiteral(
    "The American English Hunspell dictionary is unavailable. Install the en_US dictionary and reopen Crumb Writer.");

bool decodeDictionary(const QByteArray &bytes, QStringList &words)
{
    words.clear();
    if (!bytes.isValidUtf8() || bytes.contains('\0')) return false;
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    QSet<QString> keys;
    for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts)) {
        const QString word = line.trimmed();
        const QString key = SpellWords::key(word);
        if (key.isEmpty() || keys.contains(key)) continue;
        keys.insert(key);
        words.append(word);
    }
    words.sort(Qt::CaseInsensitive);
    return true;
}

struct Mark {
    QTextCursor cursor;
    QString text;

    bool valid() const {
        return !cursor.isNull() && cursor.hasSelection() && ManuscriptText::selectedText(cursor) == text;
    }
};
}

struct SpellCheck::State {
    State(SpellCheck &self, ManuscriptEditor *manuscript, QWidget *owner)
        : controller(self), editor(manuscript), window(owner)
    {
        highlights.setSingleShot(true);
        highlights.setInterval(75);
        QObject::connect(&highlights, &QTimer::timeout, &self, [this] { paint(); });
        scanning.setInterval(0);
        QObject::connect(&scanning, &QTimer::timeout, &self, [this] { scanSlice(); });
    }

    SpellCheck &controller;
    QPointer<ManuscriptEditor> editor;
    QPointer<QWidget> window;
    QPointer<SpellReview> review;
    QPointer<SpellDictionaryDialog> manager;
    TextCompanion dictionary{QStringLiteral(".dictionary")};
    QStringList personal, aliases;
    SpellWords spelling;
    QSet<QString> ignoredWords;
    QVector<Mark> ignoredOnce;
    Mark current;
    QTimer highlights, scanning;
    bool enabled = true;
    bool warned = false;
    bool modifying = false;
    bool malformed = false;
    enum class Work { None, Navigate, ReplaceAll };
    Work work = Work::None;
    int originLine = 0, origin = 0, visited = 0;
    bool reverse = false, includeContaining = false;
    QString targetKey, replacement;
    QVector<SpellingRange> matches;

    void notify(const QString &message) {
        if (controller.onNotice) controller.onNotice(message);
    }

    void stopScan() {
        scanning.stop();
        work = Work::None;
        matches.clear();
    }

    void schedule() {
        if (enabled && !highlights.isActive()) highlights.start();
    }

    void changed() {
        if (modifying) { schedule(); return; }
        if (work != Work::None) {
            stopScan();
            if (review) review->setStatus(QObject::tr("The manuscript changed. Press F8 to continue."), true);
            current = {};
        } else if (!current.cursor.isNull() && !current.valid()) {
            current = {};
            if (review) review->setStatus(QObject::tr("The passage has changed. Press F8 to continue."), true);
        }
        ignoredOnce.removeIf([](const Mark &m) { return !m.valid(); });
        schedule();
    }

    void exceptionsChanged() {
        stopScan();
        spelling.setExceptions(aliases, personal);
        if (manager) manager->updateWords(personal);
        if (review && review->isVisible()) {
            current = {};
            review->setStatus(QObject::tr("The word lists changed. Press F8 to continue."), true);
        }
        schedule();
    }

    bool readDictionary(QString *error) {
        malformed = !decodeDictionary(dictionary.bytes(), personal);
        exceptionsChanged();
        if (!malformed) return true;
        if (error) *error = QObject::tr("The dictionary %1 must be a UTF-8 text file with one entry per line.")
                            .arg(dictionary.sidecarPath());
        return false;
    }

    bool editWord(QString word, bool remove) {
        word = word.trimmed();
        const QString key = SpellWords::key(word);
        if (key.isEmpty() || word.contains(u'\n') || word.contains(u'\r') || word.contains(QChar::Null)) return false;
        auto report = [this](const QString &error) {
            if (manager && manager->isVisible()) manager->message(error);
            if (review && review->isVisible()) review->setStatus(error, false);
            notify(error);
        };
        if (malformed) {
            report(QObject::tr("Correct the dictionary file and reload it before editing its words."));
            return false;
        }
        QStringList updated = personal;
        const auto existing = std::find_if(updated.begin(), updated.end(), [&](const QString &entry) {
            return SpellWords::key(entry) == key;
        });
        if (remove) {
            if (existing == updated.end()) return false;
            updated.erase(existing);
        } else {
            if (existing != updated.end()) {
                if (manager) manager->message(QObject::tr("This spelling already belongs to the dictionary."));
                return true;
            }
            updated.append(word);
        }
        updated.sort(Qt::CaseInsensitive);
        QByteArray bytes = updated.join(u'\n').toUtf8();
        if (!updated.isEmpty()) bytes += '\n';
        QString error;
        if (!dictionary.write(bytes, &error)) { report(error); return false; }
        personal = std::move(updated);
        exceptionsChanged();
        if (controller.onChanged) controller.onChanged();
        if (manager) manager->message(remove ? QObject::tr("Word removed.") : QObject::tr("Word added."));
        return true;
    }

    QVector<SpellingRange> issuesIn(const QTextBlock &block) const {
        QVector<SpellingRange> result;
        const auto &rec = editor->model().record(block.blockNumber());
        for (auto range : spelling.issues(rec.text)) {
            if (!editor->model().isGeneric() && rec.valid &&
                range.start < rec.headerEnd && range.end > rec.headerStart) continue;
            if (!ignoredWords.isEmpty() && ignoredWords.contains(SpellWords::key(
                    rec.text.mid(range.start, range.end - range.start)))) continue;
            range.start += block.position();
            range.end += block.position();
            const bool ignored = std::any_of(ignoredOnce.cbegin(), ignoredOnce.cend(), [&](const Mark &m) {
                return m.cursor.selectionStart() == range.start && m.cursor.selectionEnd() == range.end;
            });
            if (!ignored) result.append(range);
        }
        return result;
    }

    Mark mark(SpellingRange range) const {
        QTextCursor c(editor->document());
        c.setPosition(range.start);
        c.setPosition(range.end, QTextCursor::KeepAnchor);
        return {c, ManuscriptText::selectedText(c)};
    }

    std::optional<SpellingRange> issueAt(int position) const {
        if (!editor) return {};
        const auto block = editor->document()->findBlock(position);
        if (!block.isValid()) return {};
        // At the end of a word, keyboard context menus still refer to that word.
        for (const auto hit : issuesIn(block))
            if (position >= hit.start && position <= hit.end) return hit;
        return {};
    }

    void paint() {
        if (!enabled || !editor || !editor->isVisible()) return;
        if (SpellWords::loading()) { schedule(); return; }
        if (!SpellWords::available()) {
            if (!warned) { warned = true; notify(missingDictionary); }
            return;
        }
        const auto bounds = editor->viewport()->rect();
        const auto top = editor->cursorForPosition(bounds.topLeft()).block();
        const auto bottom = editor->cursorForPosition(bounds.bottomRight()).block();
        QList<QTextEdit::ExtraSelection> selections;
        QTextCharFormat format;
        format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        format.setUnderlineColor(EditorColors::warning(editor->palette().base().color()));
        for (auto block = top; block.isValid() && block.position() <= bottom.position(); block = block.next()) {
            for (const auto hit : issuesIn(block)) {
                QTextCursor c(block);
                c.setPosition(hit.start);
                c.setPosition(hit.end, QTextCursor::KeepAnchor);
                selections.append({c, format});
            }
        }
        editor->setSpellingHighlights(selections);
    }

    void showReview() {
        if (!review) {
            review = new SpellReview(window);
            review->action = [this](SpellAction action, const QString &text) { act(action, text); };
            QObject::connect(review, &QDialog::finished, &controller, [this] {
                stopScan();
                current = {};
                if (editor) editor->setFocus();
            });
        }
        review->show();
        review->raise();
        review->activateWindow();
    }

    void present(SpellingRange range) {
        stopScan();
        current = mark(range);
        editor->selectMatch(range.start, range.end);
        if (review) review->showIssue(current.text, current.cursor.blockNumber()+1, SpellWords::suggestions(current.text));
    }

    void startNavigation(bool backward) {
        if (!editor) return;
        showReview();
        stopScan();
        if (!enabled) {
            review->setStatus(QObject::tr("Spelling is disabled in Settings."), true);
            return;
        }
        // Resolve any provisional text before recording absolute offsets.
        editor->prepareSearch();
        const auto c = editor->textCursor();
        origin = backward ? c.selectionStart() : c.selectionEnd();
        originLine = editor->document()->findBlock(origin).blockNumber();
        includeContaining = !c.hasSelection();
        reverse = backward;
        visited = 0;
        work = Work::Navigate;
        review->setStatus(QObject::tr("Checking spelling…"), true);
        current = {};
        scanning.start();
    }

    void startReplaceAll(const QString &value) {
        stopScan();
        targetKey = SpellWords::key(current.text);
        replacement = value;
        work = Work::ReplaceAll;
        visited = 0;
        review->setStatus(QObject::tr("Finding all occurrences…"), true);
        scanning.start();
    }

    void scanSlice() {
        if (!editor || !enabled || work == Work::None) { stopScan(); return; }
        if (SpellWords::loading()) { scanning.setInterval(15); return; }
        if (!SpellWords::available()) {
            stopScan();
            review->setStatus(missingDictionary, true);
            return;
        }
        if (scanning.interval()) scanning.setInterval(0);
        QElapsedTimer time;
        time.start();
        const int lines = editor->blockCount();
        do {
            if (work == Work::ReplaceAll) {
                if (visited == lines) { applyAll(); return; }
                const auto block = editor->document()->findBlockByNumber(visited++);
                const auto &text = editor->model().record(block.blockNumber()).text;
                for (const auto hit : issuesIn(block)) {
                    const auto word = text.mid(hit.start - block.position(), hit.end - hit.start);
                    if (SpellWords::key(word) == targetKey && word != replacement)
                        matches.append(hit);
                }
            } else {
                if (visited > lines) {
                    stopScan();
                    review->setStatus(QObject::tr("Spelling check complete. No misspellings found."), true);
                    return;
                }
                const int turn = visited++;
                const int line = (originLine + (reverse ? -turn : turn) + lines) % lines;
                auto hits = issuesIn(editor->document()->findBlockByNumber(line));
                if (reverse) std::reverse(hits.begin(), hits.end());
                for (const auto hit : hits) {
                    const bool after = reverse ? hit.end <= origin : hit.start >= origin;
                    const bool inside = includeContaining && hit.start <= origin && origin < hit.end;
                    if ((turn == 0 && !(after || inside)) || (turn == lines && (after || inside))) continue;
                    present(hit);
                    return;
                }
            }
        } while (time.elapsed() < 5);
    }

    void applyAll() {
        auto edits = std::move(matches);
        stopScan();
        if (!editor || editor->isReadOnly()) return;
        if (edits.isEmpty()) { review->setStatus(QObject::tr("No changes to apply."), true); return; }
        QStringList removed;
        if (editor->copiesDeletedText()) {
            for (const auto &hit : edits) {
                QTextCursor cursor(editor->document());
                cursor.setPosition(hit.start);
                cursor.setPosition(hit.end, QTextCursor::KeepAnchor);
                removed.append(ManuscriptText::selectedText(cursor));
            }
        }
        {
            QScopedValueRollback<bool> guard(modifying, true);
            QTextCursor group(editor->document());
            group.beginEditBlock();
            for (auto it = edits.crbegin(); it != edits.crend(); ++it)
                editor->replaceRangeLiteral(it->start, it->end, replacement, false);
            group.endEditBlock();
        }
        editor->copyRemovedText(removed.join(u'\n'));
        notify(QObject::tr("Replaced %1 spelling occurrences.").arg(edits.size()));
        current = {};
        startNavigation(reverse);
    }

    void act(SpellAction action, const QString &value, bool continueReview = true) {
        if (action == SpellAction::Dictionary) { controller.showDictionary(); return; }
        if (action == SpellAction::Previous || action == SpellAction::Next) {
            startNavigation(action == SpellAction::Previous);
            return;
        }
        if (!current.valid() || !editor || !enabled) return;
        const int start = current.cursor.selectionStart(), end = current.cursor.selectionEnd();
        const auto actual = issueAt(start);
        if (!actual || actual->start != start || actual->end != end) {
            if (review) review->setStatus(QObject::tr("The passage has changed. Press F8 to continue."), true);
            current = {};
            return;
        }
        if (action == SpellAction::Add) {
            if (!editWord(current.text, false)) return;
        } else if (action == SpellAction::IgnoreAll) {
            ignoredWords.insert(SpellWords::key(current.text));
        } else if (action == SpellAction::Ignore) {
            ignoredOnce.append(current);
        } else {
            const QString replacementText = value.trimmed();
            if (editor->isReadOnly() || replacementText.isEmpty()) return;
            if (action == SpellAction::ReplaceAll) { startReplaceAll(replacementText); return; }
            if (replacementText == current.text) {
                if (review) review->setStatus(QObject::tr("Choose another spelling or type a correction."), false);
                return;
            }
            QScopedValueRollback<bool> guard(modifying, true);
            const int position = editor->replaceRangeLiteral(start, end, replacementText);
            editor->selectRange(reverse ? start : position, reverse ? start : position, false);
        }
        current = {};
        schedule();
        if (continueReview && review && review->isVisible()) startNavigation(reverse);
    }

    void contextMenu(QMenu *menu, int position) {
        if (!enabled || !editor || SpellWords::loading() || !SpellWords::available()) return;
        const auto hit = issueAt(position);
        if (!hit) return;
        const Mark word = mark(*hit);
        auto *section = menu->addMenu(QObject::tr("Spelling"));
        section->setAccessibleName(QObject::tr("Spelling suggestions"));
        auto item = [&](const QString &label, SpellAction command, const QString &value = QString()) {
            QObject::connect(section->addAction(label), &QAction::triggered, &controller, [this, word, command, value] {
                if (!word.valid()) return;
                current = word;
                act(command, value);
            });
        };
        const auto suggestions = SpellWords::suggestions(word.text).mid(0, 8);
        for (const auto &s : suggestions) item(QString(s).replace(u'&', QStringLiteral("&&")), SpellAction::Replace, s);
        if (!suggestions.isEmpty()) section->addSeparator();
        item(QObject::tr("&Ignore Once"), SpellAction::Ignore);
        item(QObject::tr("Ignore A&ll"), SpellAction::IgnoreAll);
        item(QObject::tr("Add to &Dictionary"), SpellAction::Add);
        QObject::connect(section->addAction(QObject::tr("&Review…")), &QAction::triggered, &controller, [this, word] {
            if (!word.valid()) return;
            showReview();
            present({word.cursor.selectionStart(), word.cursor.selectionEnd()});
        });
    }
};

SpellCheck::SpellCheck(ManuscriptEditor *editor, QWidget *owner)
    : QObject(owner), d(std::make_unique<State>(*this, editor, owner))
{
    editor->viewport()->installEventFilter(this);
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { d->schedule(); });
    editor->model().observe(this, [this](const CrumbChange &change) {
        if (change.textChanged) d->changed();
    });
    editor->spellingMenu = [guard = QPointer<SpellCheck>(this)](QMenu *menu, int position) {
        if (guard) guard->d->contextMenu(menu, position);
    };
    d->schedule();
}

SpellCheck::~SpellCheck()
{
    if (d->review) delete d->review;
    if (d->manager) delete d->manager;
}

bool SpellCheck::eventFilter(QObject *watched, QEvent *event)
{
    if (d->editor && watched == d->editor->viewport() &&
        (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
         event->type() == QEvent::PaletteChange)) d->schedule();
    return QObject::eventFilter(watched, event);
}

void SpellCheck::setEnabled(bool enabled)
{
    if (d->enabled == enabled) return;
    d->enabled = enabled;
    if (enabled) { d->warned = false; d->schedule(); }
    else {
        d->stopScan();
        d->highlights.stop();
        d->current = {};
        if (d->editor) d->editor->setSpellingHighlights({});
        if (d->review) d->review->hide();
    }
}

void SpellCheck::setAliases(const QStringList &aliases)
{
    if (d->aliases == aliases) return;
    d->aliases = aliases;
    d->exceptionsChanged();
}

void SpellCheck::correctAtCaret()
{
    if (!d->enabled || !d->editor) return;
    if (!SpellWords::available()) {
        if (onNotice) onNotice(SpellWords::loading() ? tr("Loading the spelling dictionary…") : missingDictionary);
        return;
    }
    const auto hit = d->issueAt(d->editor->textCursor().position());
    if (!hit) return;
    const auto word = d->mark(*hit);
    const auto suggestions = SpellWords::suggestions(word.text);
    if (suggestions.isEmpty()) {
        if (onNotice) onNotice(tr("No spelling suggestions for %1.").arg(word.text));
        return;
    }
    auto *menu = new QMenu(d->editor);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName("caretCorrections");
    menu->setAccessibleName(tr("Spelling suggestions"));
    for (const auto &value : suggestions) {
        auto *action = menu->addAction(QString(value).replace(u'&', QStringLiteral("&&")));
        connect(action, &QAction::triggered, this, [this, word, value] {
            if (!word.valid()) return;
            d->current = word;
            d->act(SpellAction::Replace, value, false);
            d->editor->setFocus();
        });
    }
    menu->popup(d->editor->viewport()->mapToGlobal(d->editor->cursorRect().bottomLeft()));
    menu->setActiveAction(menu->actions().first());
}

void SpellCheck::next(bool backward) { d->startNavigation(backward); }

void SpellCheck::showDictionary()
{
    if (!d->manager) {
        d->manager = new SpellDictionaryDialog(d->window);
        d->manager->edit = [this](const QString &word, bool remove) { d->editWord(word, remove); };
        d->manager->reload = [this] {
            if (d->dictionary.manuscriptPath().isEmpty()) {
                d->manager->message(tr("Save the manuscript before reloading its dictionary."));
                return;
            }
            auto reload = [this] {
                QString error;
                const bool read = d->dictionary.openManuscript(d->dictionary.manuscriptPath(), &error);
                const bool valid = d->readDictionary(read ? &error : nullptr);
                d->manager->message(read && valid ? tr("Dictionary reloaded.") : error);
                if (onChanged) onChanged();
            };
            if (!hasPending()) { reload(); return; }
            auto *box = new QMessageBox(QMessageBox::Question, tr("Reload dictionary"),
                tr("Replace the unsaved dictionary with the copy on disk?"),
                QMessageBox::Yes | QMessageBox::Cancel, d->manager);
            box->setDefaultButton(QMessageBox::Cancel);
            box->setAttribute(Qt::WA_DeleteOnClose);
            connect(box, &QMessageBox::finished, this, [reload](int choice) {
                if (choice == QMessageBox::Yes) reload();
            });
            box->open();
        };
    }
    d->manager->updateWords(d->personal);
    d->manager->show();
    d->manager->raise();
    d->manager->activateWindow();
}

bool SpellCheck::openManuscript(const QString &path, QString *error)
{
    d->ignoredOnce.clear();
    d->ignoredWords.clear();
    d->current = {};
    if (d->review) d->review->hide();
    const bool read = d->dictionary.openManuscript(path, error);
    const bool valid = d->readDictionary(read ? error : nullptr);
    return read && valid;
}

bool SpellCheck::saveAs(const QString &path, QString *error)
{
    const bool written = d->dictionary.saveAs(path, error);
    const bool valid = d->readDictionary(written ? error : nullptr);
    if (onChanged) onChanged();
    return written && valid;
}

bool SpellCheck::hasPending() const { return d->dictionary.hasPending(); }
bool SpellCheck::flush(QString *error) { return d->dictionary.flush(error); }
