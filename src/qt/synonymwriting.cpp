#include "synonymwriting.h"
#include "editorcolors.h"
#include "manuscripteditor.h"

#include <QFrame>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QScreen>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextLayout>
#include <QVBoxLayout>
#include <algorithm>

// Only an explicit request opens this list. A popup leaves the manuscript
// caret in place, including when a mouse lookup starts elsewhere on screen.
class SynonymChoices final : public QFrame {
public:
    explicit SynonymChoices(ManuscriptEditor *editor)
        : QFrame(editor, Qt::Popup), editor_(editor), title_(new QLabel(this)),
          list_(new QListView(this)), model_(new QStringListModel(this))
    {
        setObjectName(QStringLiteral("synonymChoices"));
        setFrameShape(QFrame::StyledPanel);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(3);
        layout->addWidget(title_);
        layout->addWidget(list_);
        title_->setTextFormat(Qt::PlainText);
        title_->setBuddy(list_);
        list_->setObjectName(QStringLiteral("synonymChoiceList"));
        list_->setModel(model_);
        list_->setUniformItemSizes(true);
        list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_->installEventFilter(this);
        setFocusProxy(list_);
        connect(list_, &QListView::clicked, this, [this](const QModelIndex &index) { accept(index); });
        connect(list_, &QListView::activated, this, [this](const QModelIndex &index) { accept(index); });
    }

    void present(const QString &title, const QStringList &labels, int position, std::function<void(int)> chosen)
    {
        chosen_ = std::move(chosen);
        title_->setText(title);
        list_->setAccessibleName(title);
        model_->setStringList(labels);
        list_->setCurrentIndex(model_->index(0));
        QTextCursor source(editor_->document());
        source.setPosition(position);
        const auto caret = editor_->cursorRect(source);
        const auto anchor = editor_->viewport()->mapToGlobal(caret.topLeft());
        const auto area = editor_->screen()->availableGeometry();
        const int w = std::min({440, std::max(220, editor_->window()->width()), area.width()});
        const int h = std::min(area.height(), title_->sizeHint().height() + 16 +
                qMin(8, int(labels.size())) * qMax(list_->sizeHintForRow(0), list_->fontMetrics().height() + 2));
        resize(w, h);
        const int x = std::clamp(anchor.x(), area.left(), area.right() - w + 1);
        int y = anchor.y() + caret.height();
        if (y + h > area.bottom() + 1) y = anchor.y() - h;
        move(x, std::clamp(y, area.top(), area.bottom() - h + 1));
        show();
        list_->setFocus(Qt::PopupFocusReason);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == list_ && event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                accept(list_->currentIndex());
                return true;
            }
            if (key->key() == Qt::Key_Escape) {
                returnToEditor();
                return true;
            }
        }
        return QFrame::eventFilter(object, event);
    }

private:
    void returnToEditor()
    {
        hide();
        editor_->window()->activateWindow();
        editor_->setFocus(Qt::PopupFocusReason);
    }

    void accept(const QModelIndex &index)
    {
        if (!isVisible() || !index.isValid() || !chosen_) return;
        const int row = index.row();
        auto action = std::move(chosen_);
        returnToEditor();
        action(row);
    }

    ManuscriptEditor *editor_;
    QLabel *title_;
    QListView *list_;
    QStringListModel *model_;
    std::function<void(int)> chosen_;
};

SynonymWriting::SynonymWriting(ManuscriptEditor *editor, SynonymMatcher *matcher)
    : QObject(editor), editor_(editor), matcher_(matcher), ghost_(new QLabel(editor->viewport()))
{
    ghost_->setObjectName(QStringLiteral("synonymCompletionHint"));
    ghost_->setTextFormat(Qt::PlainText);
    ghost_->setAccessibleName(tr("Suggested completion"));
    ghost_->setFocusPolicy(Qt::NoFocus);
    ghost_->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghost_->setAttribute(Qt::WA_NoSystemBackground);
    ghost_->hide();
    highlights_.setSingleShot(true);
    connect(&highlights_, &QTimer::timeout, this, &SynonymWriting::refreshHighlights);
    editor->onTypedText = [self = QPointer<SynonymWriting>(this)] { if (self) self->afterTyping(); };
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        dismissHint();
        if (choices_) choices_->hide();
        if (editor_->textCursor().position() != pinnedPosition_) pinnedGroup_ = -1;
        scheduleHighlights();
    });
    connect(editor, &QPlainTextEdit::selectionChanged, this, [this] {
        if (editor_->textCursor().hasSelection()) dismissHint();
    });
    editor->model().observe(this, [this](const CrumbChange &change) {
        if (change.textChanged) listsChanged();
    });
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        placeHint();
        scheduleHighlights();
    });
    editor->installEventFilter(this);
    editor->viewport()->installEventFilter(this);
    scheduleHighlights();
}

SynonymWriting::~SynonymWriting()
{
    delete choices_.data();
    delete ghost_.data();
}

void SynonymWriting::applySettings(const SynonymWritingSettings &settings)
{
    settings_ = settings;
    settings_.minimumPrefix = qMax(1, settings.minimumPrefix);
    listsChanged();
}

void SynonymWriting::listsChanged()
{
    dismissHint();
    if (choices_) choices_->hide();
    pinnedGroup_ = -1;
    highlightsDirty_ = true;
    scheduleHighlights();
}

void SynonymWriting::scheduleHighlights()
{
    if (!highlights_.isActive()) highlights_.start(0);
}

void SynonymWriting::refreshHighlights()
{
    if (!matcher_ || !editor_->isVisible()) return;
    if (matcher_->groups().isEmpty() || (settings_.charactersUnderlining == SynonymUnderlining::Off &&
                                         settings_.recurringUnderlining == SynonymUnderlining::Off)) {
        if (!paintedRanges_.isEmpty()) editor_->setSynonymHighlights({});
        paintedRanges_.clear();
        return;
    }
    const auto visible = editor_->viewport()->rect();
    const auto first = editor_->cursorForPosition(visible.topLeft()).block();
    const auto last = editor_->cursorForPosition(visible.bottomRight()).block();
    const auto color = settings_.underlineColor.isValid() ? settings_.underlineColor
                    : EditorColors::synonymUnderline(editor_->palette());
    QList<int> current;
    const bool groupOnly = settings_.charactersUnderlining == SynonymUnderlining::CurrentGroup ||
                           settings_.recurringUnderlining == SynonymUnderlining::CurrentGroup;
    if (groupOnly && !editor_->virtualRows()) {
        const auto source = matcher_->mentionAt(editor_->textCursor().position());
        if (source) {
            current = source->groups;
            if (pinnedGroup_ >= 0 && current.contains(pinnedGroup_)) current = {pinnedGroup_};
        }
    }
    if (!highlightsDirty_ && visibleFirst_ == first.blockNumber() && visibleLast_ == last.blockNumber() &&
        current == paintedGroups_ && color == paintedColor_) return;

    QList<QPair<int, int>> ranges;
    for (auto block = first; block.isValid() && block.blockNumber() <= last.blockNumber(); block = block.next()) {
        const auto mentions = matcher_->mentions(block);
        for (const auto &hit : mentions) {
            const bool show = std::any_of(hit.groups.cbegin(), hit.groups.cend(), [&](int group) {
                const auto mode = matcher_->groups().at(group).source != NameSource::Characters ?
                        settings_.recurringUnderlining : settings_.charactersUnderlining;
                return mode == SynonymUnderlining::All ||
                       (mode == SynonymUnderlining::CurrentGroup && current.contains(group));
            });
            if (show) ranges.append({hit.start, hit.end});
        }
    }
    if (highlightsDirty_ || ranges != paintedRanges_ || color != paintedColor_) {
        QTextCharFormat format;
        format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        format.setUnderlineColor(color);
        QList<QTextEdit::ExtraSelection> selections;
        selections.reserve(ranges.size());
        for (const auto &range : ranges) {
            QTextCursor cursor(editor_->document());
            cursor.setPosition(range.first);
            cursor.setPosition(range.second, QTextCursor::KeepAnchor);
            selections.append({cursor, format});
        }
        editor_->setSynonymHighlights(selections);
    }
    paintedRanges_ = std::move(ranges);
    paintedGroups_ = std::move(current);
    paintedColor_ = color;
    visibleFirst_ = first.blockNumber();
    visibleLast_ = last.blockNumber();
    highlightsDirty_ = false;
}

void SynonymWriting::setStatusHint(const QString &text)
{
    if (statusHint_ == text) return;
    statusHint_ = text;
    if (onHint) onHint(text);
}

void SynonymWriting::dismissHint()
{
    suggestion_.reset();
    if (ghost_) ghost_->hide();
    setStatusHint({});
}

void SynonymWriting::afterTyping()
{
    dismissHint();
    const auto cursor = editor_->textCursor();
    if (!matcher_ || !editor_->hasFocus() || editor_->viSelectionActive() || preediting_ ||
        cursor.hasSelection() || editor_->virtualRows()) return;
    const bool characters = settings_.charactersCompletion == SynonymCompletion::Automatic;
    const bool recurring = settings_.recurringCompletion == SynonymCompletion::Automatic;
    if (!characters && !recurring) return;
    const auto candidates = matcher_->completions(cursor.position(), characters, recurring, settings_.minimumPrefix, 2);
    if (candidates.size() != 1) return;
    suggestion_ = candidates.front();
    placeHint();
}

void SynonymWriting::placeHint()
{
    if (!suggestion_ || !ghost_) return;
    const auto cursor = editor_->textCursor();
    const QRect caret = editor_->cursorRect(cursor);
    auto font = editor_->inputMethodQuery(Qt::ImFont).value<QFont>();
    for (const auto &style : cursor.block().layout()->formats()) {
        const int col = cursor.positionInBlock() - 1;
        if (col >= style.start && col < style.start + style.length)
            font = style.format.font().resolve(font);
    }
    ghost_->setFont(font);
    const int width = ghost_->fontMetrics().horizontalAdvance(suggestion_->suffix) + 2;
    const bool rtl = cursor.block().textDirection() == Qt::RightToLeft;
    const int left = rtl ? caret.left() - width : caret.right() + 1;
    const QRect box(left, caret.top(), width, caret.height());
    const auto row = cursor.block().layout()->lineForTextPosition(cursor.positionInBlock());
    // An embedded bidirectional run can leave the logical end of a line in
    // its visual middle. The status bar is also used when a suffix would
    // cover existing prose or run past the viewport.
    const bool outerEdge = row.isValid() && (rtl
            ? row.cursorToX(cursor.positionInBlock()) <= row.naturalTextRect().left() + 2
            : row.cursorToX(cursor.positionInBlock()) >= row.naturalTextRect().right() - 2);
    if (!cursor.atBlockEnd() || !outerEdge || !editor_->viewport()->rect().contains(box)) {
        ghost_->hide();
        setStatusHint(tr("F5 to complete “%1”").arg(suggestion_->alias));
        return;
    }
    setStatusHint({});
    auto palette = editor_->palette();
    palette.setColor(QPalette::WindowText, EditorColors::completionHint(palette));
    ghost_->setPalette(palette);
    ghost_->setText(suggestion_->suffix);
    ghost_->setAlignment((rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    ghost_->setGeometry(box);
    ghost_->show();
    ghost_->raise();
}

void SynonymWriting::accept(const SynonymSuggestion &candidate)
{
    // Insertion starts a separate undo step and preserves the spelling of
    // every character already typed, including a curly apostrophe.
    const auto suffix = candidate.suffix;
    dismissHint();
    editor_->insertLiteral(suffix);
}

void SynonymWriting::complete()
{
    if (!matcher_ || editor_->isReadOnly() || editor_->virtualRows() || editor_->viSelectionActive() ||
        preediting_ || editor_->textCursor().hasSelection()) return;
    if (suggestion_) { accept(*suggestion_); return; }
    const bool characters = settings_.charactersCompletion != SynonymCompletion::Off;
    const bool recurring = settings_.recurringCompletion != SynonymCompletion::Off;
    if (!characters && !recurring) {
        if (onNotice) onNotice(tr("Name completion is turned off in Settings."));
        return;
    }
    const auto position = editor_->textCursor().position();
    const auto candidates = matcher_->completions(position, characters, recurring);
    if (candidates.isEmpty()) {
        if (onNotice) onNotice(tr("No matching name to complete."));
        return;
    }
    QStringList labels;
    for (const auto &candidate : candidates) labels.append(candidate.alias);
    showChoices(tr("Complete a name"), labels, position, [this, candidates, position](int row) {
        if (editor_->textCursor().position() == position && !editor_->textCursor().hasSelection())
            accept(candidates.at(row));
    });
}

void SynonymWriting::showChoices(const QString &title, const QStringList &labels, int position,
                                std::function<void(int)> chosen)
{
    dismissHint();
    if (!choices_) choices_ = new SynonymChoices(editor_);
    choices_->present(title, labels, position, std::move(chosen));
}

void SynonymWriting::navigate(bool backward)
{
    if ((editor_->virtualRows() || !follow(editor_->textCursor().position(), backward)) && onNotice)
        onNotice(tr("Place the caret on a character, recurring name, or genre."));
}

bool SynonymWriting::follow(int position, bool backward, bool insideOnly)
{
    if (!matcher_) return false;
    auto source = matcher_->mentionAt(position);
    if (insideOnly && source && position == source->end) {
        source.reset();
        for (const auto &hit : matcher_->mentions(editor_->document()->findBlock(position))) {
            if (hit.start <= position && position < hit.end) { source = hit; break; }
        }
    }
    if (!source || source->groups.isEmpty()) return false;
    if (source->groups.size() == 1 || (source->start == pinnedPosition_ && source->groups.contains(pinnedGroup_))) {
        const int group = source->groups.size() == 1 ? source->groups.front() : pinnedGroup_;
        visit(*source, group, backward);
    } else {
        QStringList labels;
        for (const int id : source->groups) {
            const auto &group = matcher_->groups().at(id);
            QString list;
            switch (group.source) {
            case NameSource::Characters: list = tr("Characters"); break;
            case NameSource::Recurring: list = tr("Recurring"); break;
            case NameSource::Genre: list = tr("Genre"); break;
            }
            labels.append(tr("%1 · %2").arg(list, group.aliases.join(QStringLiteral(", "))));
        }
        showChoices(tr("Choose a synonym group"), labels, position, [this, source, backward](int row) {
            visit(*source, source->groups.at(row), backward);
        });
    }
    return true;
}

void SynonymWriting::visit(const SynonymMention &source, int group, bool backward)
{
    if (!matcher_) return;
    if (const auto destination = matcher_->neighbor(source, group, backward)) {
        if (jumpTo) jumpTo(destination->start);
        else editor_->jumpTo(destination->start);
        pinnedGroup_ = group;
        pinnedPosition_ = destination->start;
        scheduleHighlights();
    } else if (onNotice) {
        onNotice(backward ? tr("No earlier mention.") : tr("No later mention."));
    }
}

bool SynonymWriting::eventFilter(QObject *object, QEvent *event)
{
    if (object == editor_ && event->type() == QEvent::InputMethod) {
        preediting_ = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        if (preediting_) dismissHint();
    } else if (object == editor_ && event->type() == QEvent::FocusOut) {
        preediting_ = false;
        dismissHint();
    } else if (object == editor_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_F5) dismissHint();
    } else if (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
               event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange) {
        placeHint();
        scheduleHighlights();
    }
    return QObject::eventFilter(object, event);
}
