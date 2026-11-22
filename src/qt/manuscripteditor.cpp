#include "manuscripteditor.h"
#include "committedpunctuation.h"
#include "manuscripttext.h"
#include "viselection.h"

#include <QtCore/QScopedValueRollback>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QSyntaxHighlighter>
#include <QtGui/QTextBlock>
#include <QtWidgets/QScrollBar>

namespace {
class CrumbLayout final : public QPlainTextDocumentLayout {
public:
    using QPlainTextDocumentLayout::QPlainTextDocumentLayout;
    bool compactSeparators = false;

    QRectF blockBoundingRect(const QTextBlock &block) const override
    {
        auto bounds = QPlainTextDocumentLayout::blockBoundingRect(block);
        const auto next = block.next();
        if (!bounds.isEmpty() && next.isValid() &&
            (!compactSeparators || (block.length() > 1 && next.length() > 1))) {
            const auto *lines = block.layout();
            const qreal row = lines->lineAt(lines->lineCount() - 1).height();
            // Lay out the gap with its paragraph so Qt also accounts for it in
            // hit testing, scrolling and caret geometry. Wrapped rows stay tight.
            bounds.setHeight(bounds.height() + qMax(1, qRound(row / 4)));
        }
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
            if (change.modeChanged)
                updateTypography();
            else if (model_.isGeneric() && change.titlesChanged) {
                // A neighboring blank can change an untouched paragraph into
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
        chapterTitle_.setFont(scaledFont(font, 1.3));
        documentTitle_.setFontWeight(QFont::Bold);
        chapterTitle_.setFontWeight(QFont::Bold);

        const bool generic = model_.isGeneric();
        auto *layout = static_cast<CrumbLayout *>(document()->documentLayout());
        layout->compactSeparators = generic;
        // An empty Qt block has no characters for a highlighter to format.
        // Give the layout a small default font and the actual prose its full
        // writing font. Carets, selections and hit testing then share the
        // same compact blank row without adding characters or undo entries.
        const auto defaultFont = generic ? scaledFont(font, 0.33) : font;
        if (document()->defaultFont() != defaultFont)
            document()->setDefaultFont(defaultFont);
    }

    const QTextCharFormat &writingFormat(ParagraphRole role) const
    {
        if (role == ParagraphRole::DocumentTitle) return documentTitle_;
        if (role == ParagraphRole::ChapterTitle) return chapterTitle_;
        return body_;
    }

protected:
    void highlightBlock(const QString &text) override
    {
        // Qt extends highlighting to the next block when its predecessor's
        // state changes. Changing mode on line one restyles the whole file.
        setCurrentBlockState(model_.isGeneric() ? 1 : 0);
        if (model_.isGeneric()) {
            const auto role = model_.paragraphRole(currentBlock().blockNumber());
            if (role != ParagraphRole::Blank)
                setFormat(0, text.size(), writingFormat(role));
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
            warning.setForeground(QColor(202, 30, 45));
            setFormat(w.start, w.end - w.start, warning);
        }
    }

private:
    ManuscriptEditor *editor_;
    CrumbModel &model_;
    QTextCharFormat header_, title_, body_, documentTitle_, chapterTitle_;
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
    QScopedValueRollback<bool> guard(editing_, true);
    editRun_ = EditRun::None;
    vi_->reset();
    pendingSpace_ = {};
    punctuation_->reset();
    clearVirtual();
    clearAlignment();
    searchHighlights_.clear();
    spellingHighlights_.clear();
    setExtraSelections({});
    ManuscriptText::load(document(), normalizeInsertion(text));
    model_->markOpened();
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
    if (isVisible())
        finishOpeningViewport();
}

void ManuscriptEditor::setSmartPunctuation(bool enabled)
{
    smart_ = enabled;
    if (!enabled)
        punctuation_->reset();
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
    punctuation_->finish(document());
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
    if (!editing_ && !replaying_) {
        vi_->abandon();
        finishTyping();
        if (!pendingSpace_.isNull() && textCursor().position() != pendingSpace_.position())
            trimPending();
        clearVirtual();
    }
    refreshBand();
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
    if (!pendingSpace_.isNull() && cursor.position() != pendingSpace_.position())
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
    setExtraSelections(spellingHighlights_ + searchHighlights_);
}

void ManuscriptEditor::setSpellingHighlights(const QList<QTextEdit::ExtraSelection> &ranges)
{
    spellingHighlights_ = ranges;
    setExtraSelections(spellingHighlights_ + searchHighlights_);
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
    clearVirtual();
    clearAlignment();
    QScopedValueRollback<bool> guard(editing_, true);
    QTextCursor c(document());
    c.beginEditBlock();
    trimPending(false);
    c.setPosition(writingEnd().position());
    if (generic) {
        // A final LF terminates the last physical paragraph. Keep it after
        // the appended paragraph, including when the last paragraph is blank.
        if (!document()->isEmpty())
            ManuscriptText::insert(c, QStringLiteral("\n"));
    } else {
        while (c.atBlockStart() && c.blockNumber() > 0)
            c.movePosition(QTextCursor::PreviousCharacter);
        const bool prefix = c.blockNumber() == 0 && !model_->record(0).valid &&
            ManuscriptText::blockText(c.block()).left(c.positionInBlock()).trimmed().endsWith(QStringLiteral("//"));
        ManuscriptText::insert(c, (c.atStart() || prefix ? QString() : QStringLiteral("\n")) + header);
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
    QScopedValueRollback<bool> guard(editing_, true);
    auto cursor = at(first);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    replaceAt(cursor, {}, Input::Replacement);
    cursor.endEditBlock();
    if (cursor.atEnd())
        cursor = writingEnd();
    setTextCursor(cursor);
    trackSeparator(cursor);
    ensureCursorVisible();
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
        const int next = fields ? model_->neighborTitle(line + int(backward && inside), backward)
                                : model_->neighbor(line, backward);
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
        return highlighter_->writingFormat(role).font();
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
