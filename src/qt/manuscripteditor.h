#ifndef CRUMBWRITER_MANUSCRIPTEDITOR_H
#define CRUMBWRITER_MANUSCRIPTEDITOR_H

#include "crumbmodel.h"
#include <QtWidgets/QPlainTextEdit>
#include <functional>
#include <memory>

class CommittedPunctuation;
class CrumbHighlighter;

// All document positions are UTF-16 offsets. A virtual row is never a block in
// the document and never appears in a clipboard, save, search or word count.
class ManuscriptEditor final : public QPlainTextEdit {
public:
    explicit ManuscriptEditor(QWidget *parent = nullptr);
    ~ManuscriptEditor() override;

    CrumbModel &model() const { return *model_; }
    QString manuscriptText() const;
    void loadText(const QString &text);
    void openAtEnd();
    void setPaperPalette(const QPalette &palette);
    void setPaperActive(bool active);
    void setSmartPunctuation(bool enabled);
    void finishTyping();
    void prepareSearch();
    void clearEditorSelection();

    void jumpTo(int position, bool center = true);
    void selectRange(int anchor, int position, bool center = true);
    void selectMatch(int start, int end);
    void setSearchHighlights(const QList<QTextEdit::ExtraSelection> &ranges);
    void centerCurrentLine();
    void topCurrentCrumb();
    void appendCrumb(const QString &genre = {});
    void copyCrumb();
    void copyHeader();
    void deleteCrumb();

    int virtualRows() const { return rows_; }
    void clearVirtual();
    std::function<void()> onVirtualChanged;

    static QString normalizeInsertion(QString text, bool keepFinalSpace = false);
    int replaceRangeLiteral(int start, int end, const QString &text);
    void undo();
    void redo();
    void cut();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Alignment { Visible, Center, Top };
    enum class Input { Typed, Paste, Replacement };
    int clampPosition(int value) const;
    QTextCursor at(int position) const;
    QTextCursor writingEnd() const;
    QTextBlock crumbEdgeBlock() const;
    void navigate(bool backward, bool fields);
    void copyField(bool header);
    void onCursorMoved();
    void trimPending(bool joinPrevious = true);
    void replayEdit(bool forward);
    void trackSeparator(const QTextCursor &cursor);
    void typeText(const QString &text, int replaceStart = 0, int replaceLength = 0);
    void removeByKey(QKeyEvent *event);
    void insertSelection(const QString &text, Input kind);
    void replaceAt(QTextCursor &cursor, const QString &text, Input kind);
    void recordEditForHash(QTextCursor &cursor, const QString &text);
    void applyPaper();
    void refreshBand();
    void setVirtualRows(int rows);
    bool onLastVisualRow() const;
    int rowPitch() const;
    void align(Alignment mode);
    void clearAlignment();
    void setTopInset(int value);
    QRect virtualCaretRect() const;
    void paintCrumbBackground(QPainter &painter);

    std::unique_ptr<CrumbModel> model_;
    std::unique_ptr<CommittedPunctuation> punctuation_;
    CrumbHighlighter *highlighter_ = nullptr;
    QPalette paper_;
    bool activePaper_ = true;
    bool smart_ = true;
    bool editing_ = false;
    bool aligning_ = false;
    bool replaying_ = false;
    int activeLine_ = -1;
    QTextCursor pendingSpace_;
    int rows_ = 0;
    int virtualTop_ = 0;
    int topInset_ = 0;
    QSize outerSize_;
    bool alignQueued_ = false;
    Alignment alignment_ = Alignment::Visible;
};
#endif
