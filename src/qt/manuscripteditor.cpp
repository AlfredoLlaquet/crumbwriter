#include "manuscripteditor.h"
#include "genericlist.h"
#include "committedpunctuation.h"
#include "editorcolors.h"
#include "guillemetmarkup.h"
#include "manuscriptlinks.h"
#include "manuscripttext.h"
#include "manuscriptspacing.h"
#include "viselection.h"

#include <QtCore/QScopedValueRollback>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QFontMetricsF>
#include <QtGui/QGuiApplication>
#include <QtGui/QSyntaxHighlighter>
#include <QtGui/QTextBlock>
#include <QtWidgets/QScrollBar>
#include <array>
#include <algorithm>

namespace {
class CrumbLayout final : public QPlainTextDocumentLayout {
public:
    using QPlainTextDocumentLayout::QPlainTextDocumentLayout;
    bool compactSeparators = false;
    bool hasTitleSpacer = false;
    int activeLine = -1;
    const CrumbModel *model = nullptr;
    qreal sectionSpacing = 0;

    QRectF blockBoundingRect(const QTextBlock &block) const override
    {
        if (!block.isVisible()) return {};
        auto bounds = QPlainTextDocumentLayout::blockBoundingRect(block);
        // An empty block has no characters for the highlighter to style.
        // Only the spacer immediately after the document title needs a
        // shorter advance. Restore the native writing row while editing it.
        auto next = block.next();
        while (next.isValid() && !next.isVisible()) next = next.next();
        const bool beforeHeading = compactSeparators && next.isValid() && model &&
                                   model->headingLevel(next.blockNumber()) > 0;
        const qreal headingGap = beforeHeading ? sectionSpacing : 0;
        if (hasTitleSpacer && block.blockNumber() == 1 && activeLine != 1 &&
            block.layout()->preeditAreaText().isEmpty()) {
            bounds.setHeight(qMax<qreal>(1, bounds.height() / 3) + headingGap);
            return bounds;
        }
        if (!bounds.isEmpty() && next.isValid() &&
            (!compactSeparators || (block.length() > 1 && next.length() > 1))) {
            const auto *lines = block.layout();
            const qreal row = lines->lineAt(lines->lineCount() - 1).height();
            // Lay out the gap with its paragraph so Qt also accounts for it in
            // hit testing, scrolling and caret geometry. Wrapped rows stay tight.
            bounds.setHeight(bounds.height() + qMax(1, qRound(row / 4)));
        }
        bounds.setHeight(bounds.height() + headingGap);
        return bounds;
    }
};

QFont scaledFont(QFont font, qreal scale)
{
    if (font.pixelSize() > 0)
        font.setPixelSize(qMax(1, qRound(font.pixelSize() * scale)));
    else
        font.setPointSizeF(qMax<qreal>(1.0, font.pointSizeF() * scale));
    return font;
}
}

class CrumbHighlighter final : public QSyntaxHighlighter {
public:
    CrumbHighlighter(ManuscriptEditor *editor, CrumbModel &model)
        : QSyntaxHighlighter(editor->document()), editor_(editor), model_(model)
    {
        header_.setFontItalic(true);
        title_.setFontWeight(QFont::Bold);
        warningInk_ = EditorColors::warning(editor_->palette().base().color());
        updateTypography();
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] {
            // A structural edit can change every subsequent header warning.
            // Repaint in bounded batches after input and visible text layout.
            if (!pending_.isEmpty() && editor_->isVisible()) {
                const int first = editor_->cursorForPosition(QPoint(0, 0)).blockNumber();
                const int last = editor_->cursorForPosition(editor_->viewport()->rect().bottomRight()).blockNumber();
                for (int n = first; n <= last; ++n) {
                    if (pending_.remove(n))
                        rehighlightBlock(document()->findBlockByNumber(n));
                }
            }
            for (int count = 0; count < 48 && !pending_.isEmpty(); ++count) {
                auto it = pending_.begin();
                const int line = *it;
                pending_.erase(it);
                const auto block = document()->findBlockByNumber(line);
                if (block.isValid())
                    rehighlightBlock(block);
            }
            if (!pending_.isEmpty())
                timer_.start(0);
        });
        model_.observe(this, [this](const CrumbChange &change) {
            auto *layout = static_cast<CrumbLayout *>(document()->documentLayout());
            const bool spacer = model_.paragraphRole(1) == ParagraphRole::TitleSpacer;
            if (layout->hasTitleSpacer != spacer) {
                layout->hasTitleSpacer = spacer;
                pending_.insert(1);
            }
            if (change.modeChanged)
                updateTypography();
            else if (model_.isGeneric() && (change.titlesChanged || change.markerConfirmed)) {
                // A neighboring marker can change an untouched paragraph into
                // a heading. Qt already highlights the edited blocks.
                pending_.insert(change.firstLine);
                pending_.insert(change.lastLine);
            }
            for (int line : change.otherLines)
                pending_.insert(line);
            if (!pending_.isEmpty() && !timer_.isActive())
                timer_.start(0);
        });
    }

    void updateTypography()
    {
        const auto font = editor_->font();
        body_.setFont(font);
        documentTitle_.setFont(scaledFont(font, 1.7));
        documentTitle_.setFontWeight(QFont::Bold);
        constexpr std::array<qreal, 4> sizes{1.48, 1.30, 1.16, 1.0};
        constexpr std::array<QTextCharFormat::UnderlineStyle, 4> lines{
            QTextCharFormat::SingleUnderline, QTextCharFormat::SingleUnderline,
            QTextCharFormat::DotLine, QTextCharFormat::DotLine};
        for (size_t i = 0; i < chapterTitles_.size(); ++i) {
            chapterTitles_[i].setFont(scaledFont(font, sizes[i]));
            chapterTitles_[i].setFontWeight(QFont::Bold);
            chapterTitles_[i].setUnderlineStyle(editor_->showManuscriptMarkup() ? lines[i] : QTextCharFormat::NoUnderline);
        }
        auto *layout = static_cast<CrumbLayout *>(document()->documentLayout());
        const qreal writingHeight = QFontMetricsF(font, layout->paintDevice()).height();
        const bool generic = model_.isGeneric();
        layout->compactSeparators = generic;
        layout->sectionSpacing = qMax<qreal>(2, writingHeight * 0.45);
        layout->hasTitleSpacer = model_.paragraphRole(1) == ParagraphRole::TitleSpacer;
        if (document()->defaultFont() != font)
            document()->setDefaultFont(font);
    }

    void setActiveLine(int line)
    {
        auto *layout = static_cast<CrumbLayout *>(document()->documentLayout());
        const int previous = layout->activeLine;
        if (previous == line) return;
        layout->activeLine = line;
        if (!model_.isGeneric()) return;
        for (int changed : {previous, line}) {
            const auto role = model_.paragraphRole(changed);
            const auto block = document()->findBlockByNumber(changed);
            if (!block.isValid()) continue;
            if (role == ParagraphRole::TitleMarker) rehighlightBlock(block);
            if (role == ParagraphRole::TitleSpacer) document()->markContentsDirty(block.position(), block.length());
        }
    }

    void updateWarningInk()
    {
        const QColor next = EditorColors::warning(editor_->palette().base().color());
        if (next == warningInk_) return;
        warningInk_ = next;
        if (model_.isGeneric()) return;
        for (int line = 0; line < document()->blockCount(); ++line) {
            if (!model_.record(line).warnings.isEmpty()) pending_.insert(line);
        }
        if (!pending_.isEmpty()) timer_.start(0);
    }

    void updateGuillemetCaret()
    {
        const auto caret = editor_->textCursor();
        const auto block = model_.isGeneric() && !editor_->literalGuillemets() &&
                           !editor_->showManuscriptMarkup() && !editor_->virtualRows()
                         ? caret.block() : QTextBlock();
        const auto previous = guillemetBlock_;
        const int previousOpen = revealedOpen_;
        if (block != previous || (block.isValid() && block.revision() != guillemetRevision_)) {
            guillemetBlock_ = block;
            guillemetRevision_ = block.isValid() ? block.revision() : -1;
            currentPairs_ = block.isValid() ? GuillemetMarkup::pairs(block.text())
                                            : QList<GuillemetMarkup::Span>();
        }
        const int position = caret.positionInBlock();
        const auto match = std::lower_bound(currentPairs_.cbegin(), currentPairs_.cend(), position,
            [](const GuillemetMarkup::Span &span, int offset) { return span.close < offset; });
        revealedOpen_ = match != currentPairs_.cend() && match->containsCaret(position) ? match->open : -1;
        if (block == previous && revealedOpen_ == previousOpen) return;
        if (previousOpen >= 0 && previous.isValid() && previous != block) rehighlightBlock(previous);
        if (block.isValid() && (revealedOpen_ >= 0 || (block == previous && previousOpen >= 0)))
            rehighlightBlock(block);
    }

    const QTextCharFormat &writingFormat(ParagraphRole role, int level) const
    {
        if (role == ParagraphRole::DocumentTitle) return documentTitle_;
        if (role == ParagraphRole::ChapterTitle && level >= 1 && level <= 4)
            return chapterTitles_[size_t(level - 1)];
        return body_;
    }

protected:
    void highlightBlock(const QString &text) override
    {
        // Qt extends highlighting to the next block when its predecessor's
        // state changes. Changing mode on line one restyles the whole file.
        setCurrentBlockState(model_.isGeneric() ? 1 : 0);
        if (model_.isGeneric()) {
            const int line = currentBlock().blockNumber();
            const auto role = model_.paragraphRole(line);
            const auto *layout = static_cast<CrumbLayout *>(document()->documentLayout());
            auto format = writingFormat(role, model_.headingLevel(line));
            if (role == ParagraphRole::TitleMarker) {
                if (editor_->showManuscriptMarkup() && line != layout->activeLine) {
                    // The viewport paints the repeated glyphs to the actual title width.
                    format.setForeground(Qt::transparent);
                    format.setFontLetterSpacing(1);
                }
            }
            setFormat(0, text.size(), format);
            applyGuillemets(text);
            underlineWebLinks(text);
            return;
        }
        const auto &r = model_.record(currentBlock().blockNumber());
        if (r.valid) {
            setFormat(r.headerStart, r.headerEnd - r.headerStart, header_);
            setFormat(r.titleStart, r.titleEnd - r.titleStart, title_);
        }
        for (const CrumbWarning &w : r.warnings) {
            auto warning = w.genre.isEmpty() ? header_ : title_;
            warning.setFontWeight(QFont::Bold);
            warning.setForeground(warningInk_);
            setFormat(w.start, w.end - w.start, warning);
        }
        underlineWebLinks(text);
    }

private:
    void applyGuillemets(const QString &text)
    {
        if (editor_->literalGuillemets()) return;
        const auto caret = editor_->textCursor();
        const int position = !editor_->virtualRows() && caret.block() == currentBlock()
                           ? caret.positionInBlock() : -1;
        for (const auto &span : GuillemetMarkup::pairs(text)) {
            // The entire Generic paragraph has its writing/heading format at
            // this stage. Preserve its size, weight and underline in italics.
            auto italic = format(span.open);
            italic.setFontItalic(true);
            setFormat(span.open + 1, span.close - span.open - 1, italic);
            if (editor_->showManuscriptMarkup() || span.containsCaret(position)) continue;
            QTextCharFormat hidden;
            hidden.setForeground(Qt::transparent);
            hidden.setProperty(QTextFormat::FontPixelSize, 1);
            hidden.setFontLetterSpacingType(QFont::PercentageSpacing);
            hidden.setFontLetterSpacing(1);
            setFormat(span.open, 1, hidden);
            setFormat(span.close, 1, hidden);
        }
    }

    void underlineWebLinks(const QString &text)
    {
        for (const auto &link : ManuscriptLinks::findAll(text)) {
            // Preserve the ink and typeface of every field the address spans.
            for (int start = link.start; start < link.end;) {
                auto ink = format(start);
                int end = start + 1;
                while (end < link.end && format(end) == ink) ++end;
                ink.setFontUnderline(true);
                setFormat(start, end - start, ink);
                start = end;
            }
        }
    }

    ManuscriptEditor *editor_;
    QTextBlock guillemetBlock_;
    int guillemetRevision_ = -1;
    int revealedOpen_ = -1;
    QList<GuillemetMarkup::Span> currentPairs_;
    CrumbModel &model_;
    QTextCharFormat header_, title_, body_, documentTitle_;
    std::array<QTextCharFormat, 4> chapterTitles_;
    QColor warningInk_;
    QSet<int> pending_;
    QTimer timer_;
};

ManuscriptEditor::ManuscriptEditor(QWidget *parent)
    : QPlainTextEdit(parent),
      punctuation_(std::make_unique<CommittedPunctuation>()),
      vi_(std::make_unique<ViSelection>(*this)),
      paper_(palette())
{
    auto *manuscript = new QTextDocument(this);
    auto *layout = new CrumbLayout(manuscript);
    layout->setPaintDevice(viewport());
    manuscript->setDocumentLayout(layout);
    setDocument(manuscript);
    model_ = std::make_unique<CrumbModel>(manuscript);
    layout->model = model_.get();
    setObjectName(QStringLiteral("manuscript"));
    setFont(QFont(QStringLiteral("Serif"), 14));
    setTabChangesFocus(false);
    setLineWrapMode(WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCenterOnScroll(true); // Normal editing allows the last row to reach the top.
    document()->setDocumentMargin(8);
    highlighter_ = new CrumbHighlighter(this, *model_);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]{ onCursorMoved(); });
    model_->observe(this, [this](const CrumbChange &change) {
        syncMarkerVisibility(change.firstLine, change.lastLine);
        if (change.textChanged && !editing_ && !replaying_) {
            editRun_ = EditRun::None;
            vi_->abandon();
            punctuation_->reset();
            clearVirtual();
        }
        refreshBand();
    });
    connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, [this]{ clearAlignment(); });
    applyPaper();
}

ManuscriptEditor::~ManuscriptEditor()
{
    // Removing a syntax highlighter can emit document and cursor signals.
    disconnect(this, nullptr, this, nullptr);
    static_cast<CrumbLayout *>(document()->documentLayout())->model = nullptr;
    delete highlighter_;
}

int ManuscriptEditor::clampPosition(int value) const
{
    return qBound(0, value, document()->characterCount() - 1);
}

QTextCursor ManuscriptEditor::at(int position) const
{
    QTextCursor c(document());
    c.setPosition(clampPosition(position));
    return c;
}

QString ManuscriptEditor::manuscriptText() const
{
    return ManuscriptText::documentText(document());
}

QTextCursor ManuscriptEditor::writingEnd() const
{
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    if (cursor.atBlockStart() && cursor.blockNumber() > 0)
        cursor.movePosition(QTextCursor::PreviousCharacter);
    return cursor;
}

void ManuscriptEditor::loadText(const QString &text)
{
    QScopedValueRollback<bool> loading(loadingText_, true);
    QScopedValueRollback<bool> guard(editing_, true);
    editRun_ = EditRun::None;
    replayedAppend_ = nullptr;
    vi_->reset();
    pendingSpace_ = {};
    declinedListParagraph_ = {};
    punctuation_->reset();
    clearVirtual();
    clearAlignment();
    searchHighlights_.clear();
    spellingHighlights_.clear();
    synonymHighlights_.clear();
    setExtraSelections({});
    QString manuscript = normalizeInsertion(text);
    const int trimmedRows = ManuscriptSpacing::trimBlankEnd(manuscript, false);
    ManuscriptText::load(document(), manuscript);
    model_->markOpened(trimmedRows);
    document()->setModified(false);
    openAtEnd();
    refreshBand();
}

void ManuscriptEditor::openAtEnd()
{
    clearVirtual();
    clearAlignment();
    // Qt clamps the last page to the real text when the hidden window is shown.
    // This also restores the preceding writing after reopening a top-aligned file.
    setCenterOnScroll(false);
    setTextCursor(writingEnd());
    settleVisibleCursor();
    if (isVisible())
        finishOpeningViewport();
}

void ManuscriptEditor::setSmartPunctuation(bool enabled)
{
    smart_ = enabled;
    if (!enabled)
        punctuation_->reset();
}

void ManuscriptEditor::syncMarkerVisibility(int first, int last)
{
    auto *doc = document();
    auto block = doc->findBlockByNumber(qMax(0, first));
    if (!block.isValid()) return;
    const int start = block.position();
    bool changed = false;
    for (; block.isValid() && block.blockNumber() <= last; block = block.next()) {
        const bool visible = showManuscriptMarkup_ || !model_->isGeneric() ||
                             model_->paragraphRole(block.blockNumber()) != ParagraphRole::TitleMarker;
        if (block.isVisible() == visible) continue;
        block.setVisible(visible);
        block.setLineCount(visible ? qMax(1, block.layout()->lineCount()) : 0);
        changed = true;
    }
    if (changed) {
        const int end = block.isValid() ? block.position() : doc->characterCount();
        doc->markContentsDirty(start, end - start);
        auto *layout = static_cast<QPlainTextDocumentLayout *>(doc->documentLayout());
        layout->requestUpdate();
        Q_EMIT layout->documentSizeChanged(layout->documentSize());
        if (onMarkupVisibilityChanged) onMarkupVisibilityChanged();
    }
}

QTextCursor ManuscriptEditor::visibleEnd() const
{
    auto block = document()->lastBlock();
    while (!block.isVisible() && block.previous().isValid()) block = block.previous();
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::EndOfBlock);
    return cursor;
}

void ManuscriptEditor::settleVisibleCursor()
{
    auto cursor = textCursor();
    auto visible = [this](int pos) {
        const auto block = document()->findBlock(pos);
        return block.isValid() && !block.isVisible() ? block.position() - 1 : pos;
    };
    const int anchor = visible(cursor.anchor()), position = visible(cursor.position());
    if (anchor != cursor.anchor() || position != cursor.position()) {
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
    }
}

void ManuscriptEditor::setShowManuscriptMarkup(bool show)
{
    if (showManuscriptMarkup_ == show) return;
    showManuscriptMarkup_ = show;
    syncMarkerVisibility(0, document()->blockCount() - 1);
    settleVisibleCursor();
    if (model_->isGeneric()) {
        highlighter_->updateTypography();
        highlighter_->updateGuillemetCaret();
        highlighter_->rehighlight();
        viewport()->update();
    }
}

void ManuscriptEditor::setLiteralGuillemets(bool enabled)
{
    if (literalGuillemets_ == enabled) return;
    literalGuillemets_ = enabled;
    if (model_->isGeneric()) {
        highlighter_->updateGuillemetCaret();
        highlighter_->rehighlight();
        if (onLiteralGuillemetsChanged) onLiteralGuillemetsChanged();
    }
}

void ManuscriptEditor::finishTyping()
{
    if (editing_) return;
    const auto previous = editRun_;
    editRun_ = EditRun::None;
    if (!punctuation_->pending()) return;
    QScopedValueRollback<bool> guard(editing_, true);
    auto c = textCursor();
    if (previous == EditRun::Typing) c.joinPreviousEditBlock();
    else c.beginEditBlock();
    punctuation_->finish(document(), model_->isGeneric());
    c.endEditBlock();
}

void ManuscriptEditor::prepareSearch()
{
    finishTyping();
    trimPending();
    clearVirtual();
}

void ManuscriptEditor::onCursorMoved()
{
    settleVisibleCursor();
    if (!editing_ && !replaying_) {
        vi_->abandon();
        finishTyping();
        if (!pendingSpace_.isNull() && !insidePendingSpaces(textCursor().position()))
            trimPending();
        clearVirtual();
    }
    if (!editing_) model_->confirmMarkerOutside(rows_ ? -1 : textCursor().blockNumber());
    refreshBand();
}

void ManuscriptEditor::refreshBand()
{
    const int n = textCursor().blockNumber();
    const int line = !rows_ && (model_->isGeneric() || model_->record(n).valid) ? n : -1;
    if (highlighter_) {
        highlighter_->setActiveLine(line);
        highlighter_->updateGuillemetCaret();
    }
    if (activeLine_ != line) {
        activeLine_ = line;
        viewport()->update();
    }
}

void ManuscriptEditor::clearEditorSelection()
{
    clearVirtual();
    auto cursor = textCursor();
    cursor.clearSelection();
    setTextCursor(cursor);
}

void ManuscriptEditor::jumpTo(int position, bool center)
{
    selectRange(position, position, center);
}

void ManuscriptEditor::selectRange(int anchor, int position, bool center)
{
    auto cursor = at(anchor);
    cursor.setPosition(clampPosition(position), QTextCursor::KeepAnchor);
    finishTyping();
    if (!pendingSpace_.isNull() && !insidePendingSpaces(cursor.position()))
        trimPending();
    clearVirtual();
    clearAlignment();
    setTextCursor(cursor);
    if (center)
        centerCurrentLine();
    else
        ensureCursorVisible();
}

void ManuscriptEditor::selectMatch(int start, int end)
{
    selectRange(end, start, true);
}

void ManuscriptEditor::setSearchHighlights(const QList<QTextEdit::ExtraSelection> &ranges)
{
    searchHighlights_ = ranges;
    setExtraSelections(synonymHighlights_ + spellingHighlights_ + searchHighlights_);
}

void ManuscriptEditor::setSpellingHighlights(const QList<QTextEdit::ExtraSelection> &ranges)
{
    spellingHighlights_ = ranges;
    setExtraSelections(synonymHighlights_ + spellingHighlights_ + searchHighlights_);
}

void ManuscriptEditor::setSynonymHighlights(const QList<QTextEdit::ExtraSelection> &ranges)
{
    synonymHighlights_ = ranges;
    setExtraSelections(synonymHighlights_ + spellingHighlights_ + searchHighlights_);
}

// Qt owns this marker in the same transaction as the appended text. A joined
// whitespace cleanup, undo/redo and disposal of a redo branch all follow it.
class ManuscriptEditor::AppendUndoItem final : public QAbstractUndoItem {
public:
    explicit AppendUndoItem(ManuscriptEditor &editor)
        : editor_(editor), anchor_(editor.textCursor().anchor()), position_(editor.textCursor().position()),
          topBlock_(editor.firstVisibleBlock().blockNumber()),
          topRow_(editor.verticalScrollBar()->value() - editor.firstVisibleBlock().firstLineNumber()),
          inset_(editor.topInset_), virtualRows_(editor.rows_), virtualTop_(editor.virtualTop_),
          alignment_(editor.alignment_), size_(editor.size()) {}

    void undo() override { editor_.replayedAppend_ = this; }
    void redo() override { editor_.replayedAppend_ = this; }

    void restore(bool forward) const
    {
        if (forward) {
            auto cursor = QTextCursor(editor_.document()->findBlockByNumber(bodyLine));
            cursor.movePosition(QTextCursor::EndOfBlock);
            editor_.setTextCursor(cursor);
            editor_.ensureCursorVisible();
            return;
        }
        auto cursor = editor_.at(anchor_);
        cursor.setPosition(editor_.clampPosition(position_), QTextCursor::KeepAnchor);
        editor_.setTextCursor(cursor);
        editor_.setVirtualRows(virtualRows_);
        const bool resized = editor_.size() != size_;
        if (resized && (alignment_ != Alignment::Visible || virtualRows_)) {
            editor_.align(alignment_);
            return;
        }
        editor_.alignment_ = alignment_;
        editor_.setTopInset(inset_);
        const auto top = editor_.document()->findBlockByNumber(topBlock_);
        if (top.isValid()) {
            editor_.blockBoundingRect(top);
            editor_.verticalScrollBar()->setValue(top.firstLineNumber() + topRow_);
        }
        editor_.virtualTop_ = virtualTop_;
        if (resized && !editor_.viewport()->rect().contains(editor_.cursorRect().center()))
            editor_.ensureCursorVisible();
        editor_.viewport()->update();
    }

    int bodyLine = 0;
private:
    ManuscriptEditor &editor_;
    int anchor_, position_, topBlock_, topRow_, inset_, virtualRows_, virtualTop_;
    Alignment alignment_;
    QSize size_;
};

void ManuscriptEditor::replayEdit(bool forward)
{
    QScopedValueRollback<bool> guard(replaying_, true);
    editRun_ = EditRun::None;
    punctuation_->reset();
    pendingSpace_ = {};
    clearVirtual();
    clearAlignment();
    replayedAppend_ = nullptr;
    if (forward) QPlainTextEdit::redo();
    else QPlainTextEdit::undo();
    // Qt sets its own caret after replaying document commands. Restore the
    // writing view once that native operation has returned.
    if (replayedAppend_) {
        replayedAppend_->restore(forward);
        replayedAppend_ = nullptr;
    }
    trackSeparator(textCursor());
}

void ManuscriptEditor::appendCrumb(const QString &genre)
{
    if (isReadOnly())
        return;
    const bool generic = model_->isGeneric();
    QString header;
    if (generic) {
        if (!genre.isEmpty()) return;
    } else {
        const QString title = genre.isEmpty() ? QString() : model_->nextGenreTitle(genre);
        if (!genre.isEmpty() && title.isEmpty()) return;
        header = model_->nextHeader(title);
        if (header.isEmpty()) return;
    }
    finishTyping();
    const bool recordView = document()->isUndoRedoEnabled() && (!generic || !document()->isEmpty());
    auto view = recordView ? std::make_unique<AppendUndoItem>(*this) : nullptr;
    clearVirtual();
    clearAlignment();
    QScopedValueRollback<bool> guard(editing_, true);
    QTextCursor c = textCursor();
    c.beginEditBlock();
    trimPending(false);
    c.setPosition(writingEnd().position());
    if (generic) {
        // A final LF terminates the last physical paragraph. Keep it after
        // the appended paragraph, including when the last paragraph is blank.
        if (!document()->isEmpty()) {
            const QString prefix = GenericList::nextPrefix(c.block());
            ManuscriptText::insert(c, QStringLiteral("\n"));
            insertListPrefix(c, prefix);
        }
    } else {
        while (c.atBlockStart() && c.blockNumber() > 0)
            c.movePosition(QTextCursor::PreviousCharacter);
        const bool prefix = c.blockNumber() == 0 && !model_->record(0).valid &&
            ManuscriptText::blockText(c.block()).left(c.positionInBlock()).trimmed().endsWith(QStringLiteral("//"));
        ManuscriptText::insert(c, (c.atStart() || prefix ? QString() : QStringLiteral("\n")) + header);
    }
    if (view) {
        view->bodyLine = c.blockNumber();
        document()->appendUndoItem(view.release());
    }
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
    setFocus();
    ensureCursorVisible();
}

void ManuscriptEditor::copyField(bool header)
{
    if (header && model_->isGeneric())
        return;
    if (rows_) {
        clearVirtual();
        return;
    }
    const int line = textCursor().blockNumber();
    prepareSearch();
    const auto &r = model_->record(line);
    if (!model_->isGeneric() && r.headerEnd <= r.digitsStart)
        return;
    const auto block = document()->findBlockByNumber(line);
    QTextCursor c(block);
    if (!model_->isGeneric())
        c.setPosition(block.position() + (header ? r.headerStart : r.recordStart));
    c.setPosition(block.position() + (header ? r.headerEnd : block.length() - 1),
                  QTextCursor::KeepAnchor);
    setTextCursor(c);
    const QString selected = ManuscriptText::selectedText(c);
    QGuiApplication::clipboard()->setText(header ? selected.toLower() : selected);
    setFocus();
}

void ManuscriptEditor::copyCrumb() { copyField(false); }
void ManuscriptEditor::copyHeader() { copyField(true); }

void ManuscriptEditor::deleteCrumb()
{
    if (rows_) { clearVirtual(); return; }
    if (isReadOnly())
        return;
    const int line = textCursor().blockNumber();
    finishTyping();
    const auto &r = model_->record(line);
    const bool generic = model_->isGeneric();
    if (!generic && r.headerEnd <= r.digitsStart)
        return;
    auto block = document()->findBlockByNumber(line);
    if (generic && !block.next().isValid() && block.length() == 1)
        return;
    int first = block.position() + (!generic && r.recordStart ? r.headerStart : 0);
    int end = block.position() + block.length() - 1;
    if (block.next().isValid()) {
        if (generic || line > 0 || block.next().length() > 1 || block.next().next().isValid())
            ++end;
    } else if (line > 0) {
        first = block.position() - 1;
    }
    if (generic && block.next().isValid() && !block.next().isVisible()) {
        const auto after = block.next().next();
        if (after.isValid()) end = after.position();
        else {
            end = block.position() + block.length() - 1;
            if (first > 0) --first;
        }
    }
    if (!document()->findBlock(first).isVisible()) first = document()->findBlock(first).position() - 1;
    QScopedValueRollback<bool> guard(editing_, true);
    auto cursor = at(first);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    const QString removed = removedSelection(cursor);
    cursor.beginEditBlock();
    replaceAt(cursor, {}, Input::Replacement);
    cursor.endEditBlock();
    if (cursor.atEnd())
        cursor = writingEnd();
    setTextCursor(cursor);
    trackSeparator(cursor);
    ensureCursorVisible();
    copyRemovedText(removed);
}

QTextBlock ManuscriptEditor::crumbEdgeBlock() const
{
    auto block = textCursor().block();
    if (model_->isGeneric())
        return block;
    if (textCursor().atEnd()) {
        while (block.length() == 1 && block.previous().isValid())
            block = block.previous();
    }
    return block;
}

void ManuscriptEditor::moveTo(int position, bool extend)
{
    selectRange(extend ? textCursor().anchor() : position, position, false);
}

bool ManuscriptEditor::moveByControlKey(int key, bool extend)
{
    switch (key) {
    case Qt::Key_Left: case Qt::Key_Right: {
        const bool right = key == Qt::Key_Right;
        const auto current = textCursor();
        auto destination = current;
        destination.movePosition(right ? QTextCursor::NextWord : QTextCursor::PreviousWord);
        const int from = current.position(), to = destination.position();
        const int step = right ? 1 : -1;
        for (int pos = from + step; right ? pos <= to : pos >= to; pos += step) {
            for (const int dash : {pos - 1, pos}) {
                if (dash > 0 && document()->characterAt(dash) == QChar(0x2014) &&
                    document()->characterAt(dash - 1).isLetterOrNumber() &&
                    document()->characterAt(dash + 1).isLetterOrNumber()) {
                    moveTo(pos, extend);
                    return true;
                }
            }
        }
        return false;
    }
    case Qt::Key_Up: case Qt::Key_Down:
        navigate(key == Qt::Key_Up, false, extend);
        return true;
    case Qt::Key_Home: case Qt::Key_End: {
        const auto block = crumbEdgeBlock();
        const int start = model_->isGeneric() ? 0 : model_->record(block.blockNumber()).headerStart;
        const int column = key == Qt::Key_Home ? start : block.length() - 1;
        moveTo(block.position() + column, extend);
        return true;
    }
    case Qt::Key_PageUp:
        moveTo(0, extend);
        return true;
    case Qt::Key_PageDown: {
        if (model_->isGeneric()) {
            moveTo(model_->documentEndPosition(), extend);
            return true;
        }
        const int last = model_->neighbor(blockCount(), true);
        auto end = last >= 0 ? QTextCursor(document()->findBlockByNumber(last)) : writingEnd();
        end.movePosition(QTextCursor::EndOfBlock);
        moveTo(end.position(), extend);
        return true;
    }
    default: return false;
    }
}

void ManuscriptEditor::navigate(bool backward, bool fields, bool extend)
{
    const bool virt = rows_ != 0;
    const int line = virt ? blockCount() : textCursor().blockNumber();
    if (model_->isGeneric()) {
        // Reversing from the middle of a heading can reach its own start.
        const bool inside = !virt && textCursor().positionInBlock() > 0;
        int next = fields ? model_->neighborTitle(line + int(backward && inside), backward)
                                : model_->neighbor(line, backward);
        while (next >= 0 && !document()->findBlockByNumber(next).isVisible())
            next = model_->neighbor(next, backward);
        if (next >= 0)
            moveTo(document()->findBlockByNumber(next).position(), extend);
        return;
    }
    if (fields && !virt) {
        const auto &r = model_->record(line);
        if (r.valid) {
            const int column = textCursor().positionInBlock();
            const int stops[] = {backward ? r.bodyStart : r.titleStart,
                                 backward ? r.titleStart : r.bodyStart};
            for (int stop : stops) {
                if (backward ? stop >= column : stop <= column)
                    continue;
                moveTo(textCursor().block().position() + stop, extend);
                return;
            }
        }
    }
    const int neighbor = model_->neighbor(line, backward);
    if (neighbor < 0)
        return;
    const auto &r = model_->record(neighbor);
    const int stop = fields ? (backward ? r.bodyStart : r.titleStart) : r.headerStart;
    moveTo(document()->findBlockByNumber(neighbor).position() + stop, extend);
}

void ManuscriptEditor::focusOutEvent(QFocusEvent *e)
{
    vi_->cancel();
    finishTyping();
    QPlainTextEdit::focusOutEvent(e);
}

void ManuscriptEditor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    if (highlighter_ && event->type() == QEvent::PaletteChange)
        highlighter_->updateWarningInk();
    if (highlighter_ && (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)) {
        highlighter_->updateTypography();
        if (model_->isGeneric())
            highlighter_->rehighlight();
    }
}

QVariant ManuscriptEditor::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImFont && highlighter_ && model_->isGeneric()) {
        const auto role = model_->paragraphRole(textCursor().blockNumber());
        return highlighter_->writingFormat(role, model_->headingLevel(textCursor().blockNumber())).font();
    }
    return QPlainTextEdit::inputMethodQuery(query);
}

void ManuscriptEditor::startViSelection()
{
    vi_->start();
}

bool ManuscriptEditor::viSelectionActive() const
{
    return vi_->active();
}
