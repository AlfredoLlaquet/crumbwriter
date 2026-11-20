#include "manuscripteditor.h"
#include "committedpunctuation.h"

#include <QtCore/QScopedValueRollback>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QSyntaxHighlighter>
#include <QtGui/QTextBlock>
#include <QtWidgets/QScrollBar>

class CrumbHighlighter final : public QSyntaxHighlighter {
public:
    CrumbHighlighter(ManuscriptEditor *editor, CrumbModel &model)
        : QSyntaxHighlighter(editor->document()), editor_(editor), model_(model)
    {
        header_.setFontItalic(true);
        title_.setFontWeight(QFont::Bold);
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
            for (int line : change.otherLines)
                pending_.insert(line);
            if (!pending_.isEmpty() && !timer_.isActive())
                timer_.start(0);
        });
    }

protected:
    void highlightBlock(const QString &) override
    {
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
    QTextCharFormat header_, title_;
    QSet<int> pending_;
    QTimer timer_;
};

ManuscriptEditor::ManuscriptEditor(QWidget *parent)
    : QPlainTextEdit(parent),
      model_(std::make_unique<CrumbModel>(document())),
      punctuation_(std::make_unique<CommittedPunctuation>()),
      paper_(palette())
{
    setObjectName(QStringLiteral("manuscript"));
    setFont(QFont(QStringLiteral("Serif"), 14));
    setTabChangesFocus(false);
    setLineWrapMode(WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCenterOnScroll(true); // Allows the last real row to land at the top.
    document()->setDocumentMargin(8);
    highlighter_ = new CrumbHighlighter(this, *model_);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]{ onCursorMoved(); });
    model_->observe(this, [this](const CrumbChange &change) {
        if (change.textChanged && !editing_ && !replaying_) {
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
    // toPlainText() folds a nonbreaking space into an ordinary space.
    QString text = document()->toRawText();
    return text.replace(QChar::ParagraphSeparator, u'\n');
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
    pendingSpace_ = {};
    punctuation_->reset();
    clearVirtual();
    clearAlignment();
    setExtraSelections({});
    setPlainText(normalizeInsertion(text));
    model_->markOpened();
    document()->setModified(false);
    openAtEnd();
    refreshBand();
}

void ManuscriptEditor::openAtEnd()
{
    clearVirtual();
    clearAlignment();
    setTextCursor(writingEnd());
    ensureCursorVisible();
}

void ManuscriptEditor::setSmartPunctuation(bool enabled)
{
    smart_ = enabled;
    if (!enabled)
        punctuation_->reset();
}

void ManuscriptEditor::finishTyping()
{
    if (editing_ || !punctuation_->pending())
        return;
    QScopedValueRollback<bool> guard(editing_, true);
    auto c = textCursor();
    c.beginEditBlock();
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
    setExtraSelections(ranges);
}

void ManuscriptEditor::appendCrumb(const QString &genre)
{
    if (isReadOnly())
        return;
    const QString title = genre.isEmpty() ? QString() : model_->nextGenreTitle(genre);
    if (!genre.isEmpty() && title.isEmpty())
        return;
    const QString header = model_->nextHeader(title);
    if (header.isEmpty())
        return;
    finishTyping();
    clearVirtual();
    clearAlignment();
    QScopedValueRollback<bool> guard(editing_, true);
    QTextCursor c(document());
    c.beginEditBlock();
    trimPending(false);
    c.movePosition(QTextCursor::End);
    while (c.atBlockStart() && c.blockNumber() > 0)
        c.movePosition(QTextCursor::PreviousCharacter);
    const bool prefix = c.blockNumber() == 0 && !model_->record(0).valid &&
        c.block().text().left(c.positionInBlock()).trimmed().endsWith(QStringLiteral("//"));
    c.insertText((c.atStart() || prefix ? QString() : QStringLiteral("\n")) + header);
    c.endEditBlock();
    setTextCursor(c);
    trackSeparator(c);
    setFocus();
    ensureCursorVisible();
}

void ManuscriptEditor::copyField(bool header)
{
    if (rows_) {
        clearVirtual();
        return;
    }
    const int line = textCursor().blockNumber();
    prepareSearch();
    const auto &r = model_->record(line);
    if (r.headerEnd <= r.digitsStart)
        return;
    const auto block = document()->findBlockByNumber(line);
    QTextCursor c(block);
    c.setPosition(block.position() + (header ? r.headerStart : r.recordStart));
    c.setPosition(block.position() + (header ? r.headerEnd : block.length() - 1),
                  QTextCursor::KeepAnchor);
    setTextCursor(c);
    QGuiApplication::clipboard()->setText(c.selectedText());
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
    if (r.headerEnd <= r.digitsStart)
        return;
    auto block = document()->findBlockByNumber(line);
    int first = block.position() + (r.recordStart ? r.headerStart : 0);
    int end = block.position() + block.length() - 1;
    if (block.next().isValid()) {
        if (line > 0 || block.next().length() > 1 || block.next().next().isValid())
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
    if (textCursor().atEnd()) {
        while (block.length() == 1 && block.previous().isValid())
            block = block.previous();
    }
    return block;
}

void ManuscriptEditor::navigate(bool backward, bool fields)
{
    const bool virt = rows_ != 0;
    const int line = virt ? blockCount() : textCursor().blockNumber();
    int n = fields && !virt && model_->record(line).valid ? line : model_->neighbor(line, backward);
    const int column = textCursor().positionInBlock();
    while (n >= 0) {
        const auto &r = model_->record(n);
        const int stops[] = { r.headerStart, r.titleStart, r.bodyStart };
        for (int j = 0; j < (fields ? 3 : 1); ++j) {
            const int stop = stops[fields && backward ? 2 - j : j];
            if (n != line || (backward ? stop < column : stop > column)) {
                jumpTo(document()->findBlockByNumber(n).position() + stop, false);
                return;
            }
        }
        n = model_->neighbor(n, backward);
    }
}

void ManuscriptEditor::focusOutEvent(QFocusEvent *e)
{
    finishTyping();
    QPlainTextEdit::focusOutEvent(e);
}
