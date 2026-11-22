#include "manuscripteditor.h"

#include <QtCore/QScopedValueRollback>
#include <QtCore/QTimer>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtWidgets/QScrollBar>
#include <climits>

void ManuscriptEditor::setPaperPalette(const QPalette &palette)
{
    paper_ = palette;
    applyPaper();
}

void ManuscriptEditor::setPaperActive(bool active)
{
    if (activePaper_ != active) {
        activePaper_ = active;
        applyPaper();
    }
}

void ManuscriptEditor::applyPaper()
{
    auto colors = paper_;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const QColor base = activePaper_ ? paper_.color(group, QPalette::Base) : QColor(229, 229, 229);
        colors.setColor(group, QPalette::Base, base);
        colors.setColor(group, QPalette::Window, base);
        if (!activePaper_)
            colors.setColor(group, QPalette::Text, QColor(34, 34, 34));
    }
    setPalette(colors);
    setAutoFillBackground(true);
    viewport()->update();
}

void ManuscriptEditor::refreshBand()
{
    const int n = textCursor().blockNumber();
    const int line = !rows_ && (model_->isGeneric() || model_->record(n).valid) ? n : -1;
    if (activeLine_ != line) {
        activeLine_ = line;
        viewport()->update();
    }
}

void ManuscriptEditor::paintCrumbBackground(QPainter &p)
{
    if (activeLine_ < 0 || !activePaper_)
        return;
    auto block = firstVisibleBlock();
    if (!block.isValid() || block.blockNumber() > activeLine_)
        return;
    qreal y = contentOffset().y();
    while (block.isValid() && y <= viewport()->height()) {
        const QRectF bounds = blockBoundingRect(block);
        if (block.blockNumber() == activeLine_) {
            auto *layout = block.layout();
            if (layout->lineCount()) {
                const auto first = layout->lineAt(0).rect();
                const auto last = layout->lineAt(layout->lineCount()-1).rect();
                p.fillRect(QRectF(0, y + first.top(), viewport()->width(), last.bottom() - first.top()),
                           QColor(64, 140, 255, 31));
            }
            return;
        }
        y += bounds.height();
        block = block.next();
    }
}

void ManuscriptEditor::paintEvent(QPaintEvent *event)
{
    if (rows_ && virtualTop_) {
        QPainter p(viewport());
        p.fillRect(event->rect(), palette().base());
    } else {
        {
            QPainter p(viewport());
            paintCrumbBackground(p);
        }
        QPlainTextEdit::paintEvent(event);
    }
    if (rows_ && hasFocus()) {
        QPainter p(viewport());
        p.fillRect(virtualCaretRect(), palette().text());
    }
}

int ManuscriptEditor::rowPitch() const
{
    if (model_->isGeneric())
        return qMax(1, QFontMetrics(font(), viewport()).height());
    return qMax(1, cursorRect(at(document()->characterCount()-1)).height());
}

QRect ManuscriptEditor::virtualCaretRect() const
{
    const auto end = cursorRect(at(document()->characterCount()-1));
    const int pitch = rowPitch();
    const qint64 y = virtualTop_ ? qint64(rows_ - virtualTop_) * pitch
                                 : end.bottom() + 1 + qint64(rows_ - 1) * pitch;
    return QRect(qRound(document()->documentMargin()), int(qBound(qint64(INT_MIN)/2, y, qint64(INT_MAX)/2)), 2, pitch);
}

bool ManuscriptEditor::onLastVisualRow() const
{
    const auto c = textCursor();
    if (c.block() != document()->lastBlock())
        return false;
    // Request layout only for the active block, including after a resize.
    blockBoundingRect(c.block());
    const auto *layout = c.block().layout();
    const auto row = layout->lineForTextPosition(c.positionInBlock());
    return row.isValid() && row.lineNumber()+1 == layout->lineCount();
}

void ManuscriptEditor::setVirtualRows(int rows)
{
    rows = qMax(0, rows);
    if (rows_ == rows)
        return;
    if (rows && !rows_) {
        finishTyping();
        trimPending();
    }
    {
        QScopedValueRollback<bool> guard(editing_, true);
        rows_ = rows;
        virtualTop_ = 0;
        setCursorWidth(rows ? 0 : 1);
        if (rows) {
            auto c = at(document()->characterCount()-1);
            if (textCursor().hasSelection() || textCursor().position() != c.position())
                setTextCursor(c);
        }
        clearAlignment();
        if (rows)
            align(Alignment::Visible);
        else
            ensureCursorVisible();
    }
    refreshBand();
    viewport()->update();
    if (onVirtualChanged)
        onVirtualChanged();
}

void ManuscriptEditor::clearVirtual()
{
    if (rows_)
        setVirtualRows(0);
}

void ManuscriptEditor::setTopInset(int value)
{
    value = qBound(0, value, qMax(0, contentsRect().height()-1));
    if (topInset_ != value) {
        topInset_ = value;
        setViewportMargins(0, value, 0, 0);
    }
}

void ManuscriptEditor::clearAlignment()
{
    alignment_ = Alignment::Visible;
    setTopInset(0);
}

void ManuscriptEditor::align(Alignment mode)
{
    if (aligning_)
        return;
    QScopedValueRollback<bool> guard(aligning_, true);
    alignment_ = mode;
    setTopInset(0);
    virtualTop_ = 0;
    auto *bar = verticalScrollBar();
    const int height = qMax(1, viewport()->height());
    if (!rows_) {
        if (mode == Alignment::Visible) {
            ensureCursorVisible();
        } else if (mode == Alignment::Top) {
            const auto block = textCursor().block();
            blockBoundingRect(block);
            bar->setValue(block.firstLineNumber());
        } else {
            centerCursor();
            int y = cursorRect().center().y();
            const int middle = height/2;
            if (y > middle && bar->value() < bar->maximum()) {
                bar->setValue(bar->value()+1);
                y = cursorRect().center().y();
            }
            // A first paragraph cannot otherwise be centered by a scrollbar
            // whose first legal position is zero. The inset is view-only.
            if (y < middle)
                setTopInset(middle-y);
        }
    } else {
        const int pitch = rowPitch();
        auto r = virtualCaretRect();
        int target = mode == Alignment::Top ? 0 : (height-pitch)/2;
        if (mode == Alignment::Visible) {
            if (r.y() >= 0 && r.bottom() < height)
                return;
            target = r.y() < 0 ? 0 : qMax(0, height-pitch);
        }
        const qint64 advance = qRound64((r.y()-qreal(target))/pitch);
        bar->setValue(int(qBound(qint64(0), bar->value()+advance, qint64(bar->maximum()))));
        r = virtualCaretRect();
        // Past the last actual block, Qt has no more blocks to scroll. Draw
        // the blank rows as a view-only continuation until input materializes.
        if (r.y() > target && bar->value() == bar->maximum()) {
            virtualTop_ = qMax(1, rows_ - target/pitch);
            r = virtualCaretRect();
        }
        if (mode != Alignment::Visible && r.y() < target)
            setTopInset(target-r.y());
    }
    viewport()->update();
}

void ManuscriptEditor::centerCurrentLine() { align(Alignment::Center); }
void ManuscriptEditor::topCurrentCrumb() { align(Alignment::Top); }

void ManuscriptEditor::finishOpeningViewport()
{
    ensureCursorVisible();
    // Qt preserves the visible top line when extending the scrollbar below EOF.
    setCenterOnScroll(true);
}

void ManuscriptEditor::showEvent(QShowEvent *e)
{
    QPlainTextEdit::showEvent(e);
    if (!centerOnScroll())
        finishOpeningViewport();
}

void ManuscriptEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    const int margin = viewport()->width() > 56 ? 8 : 0;
    if (document()->documentMargin() != margin)
        document()->setDocumentMargin(margin);
    if (size() == outerSize_)
        return;
    outerSize_ = size();
    if (!aligning_ && !alignQueued_ && (alignment_ != Alignment::Visible || rows_)) {
        alignQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            alignQueued_ = false;
            if (alignment_ != Alignment::Visible || rows_)
                align(alignment_);
        });
    }
}

void ManuscriptEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && e->modifiers() == Qt::NoModifier) {
        const int y = qFloor(e->position().y());
        int rows = 0;
        if (virtualTop_) {
            rows = virtualTop_ + y / rowPitch();
        } else if (cursorForPosition(e->position().toPoint()).block() == document()->lastBlock()) {
            const QRect end = cursorRect(at(document()->characterCount()-1));
            if (y >= end.bottom()+1)
                rows = 1 + (y-end.bottom()-1)/rowPitch();
        }
        if (rows > 0) {
            setFocus(Qt::MouseFocusReason);
            setVirtualRows(rows);
            e->accept();
            return;
        }
    }
    clearVirtual();
    // Qt translates the click relative to the current viewport. Consume it
    // before removing the inset that produced those visual coordinates.
    QPlainTextEdit::mousePressEvent(e);
    clearAlignment();
}

void ManuscriptEditor::wheelEvent(QWheelEvent *e)
{
    clearVirtual();
    clearAlignment();
    QPlainTextEdit::wheelEvent(e);
}
