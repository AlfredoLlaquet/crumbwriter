#pragma once

#include <QLabel>
#include <QPointer>

class QDialog;
class QPlainTextEdit;

// Two lines in the sidebar regardless of title length. The complete title
// remains available to the keyboard, clipboard and accessibility clients.
class CrumbTitleLabel final : public QLabel {
public:
    explicit CrumbTitleLabel(QWidget *parent);
    void setTitle(const QString &title);
protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    void updatePreview();
    void showFullTitle();
    QString full_;
    QPointer<QDialog> details_;
    QPlainTextEdit *reader_ = nullptr;
};
