#include "crumbtitlelabel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QStyleOptionFocusRect>
#include <QStylePainter>
#include <QTextLayout>
#include <QVBoxLayout>

CrumbTitleLabel::CrumbTitleLabel(QWidget *parent) : QLabel(parent)
{
    setObjectName("crumbTitle");
    setAccessibleName(tr("Current crumb title"));
    setFocusPolicy(Qt::StrongFocus);
    setTextFormat(Qt::PlainText);
    setAlignment(Qt::AlignLeading | Qt::AlignTop);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(0);
    updatePreview();
}

void CrumbTitleLabel::setTitle(const QString &title)
{
    if (title == full_) return;
    full_ = title;
    setToolTip(title.isEmpty() ? QString() : QStringLiteral("<qt>%1</qt>").arg(title.toHtmlEscaped()));
    setAccessibleDescription(tr("%1. Press Enter to view the full title.").arg(title));
    if (details_) reader_->setPlainText(title);
    updatePreview();
}

void CrumbTitleLabel::updatePreview()
{
    const auto metrics = fontMetrics();
    setFixedHeight(metrics.height() + metrics.lineSpacing());
    const int available = contentsRect().width();
    if (full_.isEmpty() || available <= 0) { clear(); return; }
    QTextLayout layout(full_, font(), this);
    QTextOption options;
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(options);
    layout.beginLayout();
    auto first = layout.createLine();
    first.setLineWidth(available);
    const int length = first.textLength();
    layout.endLayout();
    if (length >= full_.size()) { setText(full_); return; }
    const auto firstLine = full_.left(length).trimmed();
    const auto remainder = QStringView(full_).mid(length).trimmed();
    setText(firstLine + u'\n' + metrics.elidedText(remainder.toString(), Qt::ElideRight, available));
}

void CrumbTitleLabel::changeEvent(QEvent *event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updatePreview();
}

void CrumbTitleLabel::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);
    if (hasFocus()) {
        QStyleOptionFocusRect focus;
        focus.initFrom(this);
        QStylePainter(this).drawPrimitive(QStyle::PE_FrameFocusRect, focus);
    }
}

void CrumbTitleLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    if (event->oldSize().width() != event->size().width()) updatePreview();
}

void CrumbTitleLabel::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        showFullTitle();
        event->accept();
        return;
    }
    QLabel::keyPressEvent(event);
}

void CrumbTitleLabel::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { showFullTitle(); event->accept(); return; }
    QLabel::mouseDoubleClickEvent(event);
}

void CrumbTitleLabel::closeFullTitle()
{
    if (details_) details_->close();
}

void CrumbTitleLabel::showFullTitle()
{
    if (!details_) {
        details_ = new QDialog(this);
        details_->setObjectName("fullCrumbTitle");
        details_->setWindowTitle(tr("Current Crumb Title"));
        details_->setAttribute(Qt::WA_DeleteOnClose);
        auto *layout = new QVBoxLayout(details_);
        reader_ = new QPlainTextEdit(details_);
        reader_->setAccessibleName(tr("Full crumb title"));
        reader_->setReadOnly(true);
        reader_->setTabChangesFocus(true);
        reader_->setPlainText(full_);
        layout->addWidget(reader_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, details_);
        connect(buttons, &QDialogButtonBox::rejected, details_, &QDialog::reject);
        layout->addWidget(buttons);
        details_->resize(fontMetrics().averageCharWidth() * 64, fontMetrics().lineSpacing() * 12);
    }
    details_->show();
    details_->raise();
    details_->activateWindow();
    reader_->setFocus(Qt::ShortcutFocusReason);
}
