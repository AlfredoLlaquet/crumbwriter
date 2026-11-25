#ifndef CRUMBWRITER_MANUSCRIPTEDITOR_H
#define CRUMBWRITER_MANUSCRIPTEDITOR_H

#include "crumbmodel.h"
#include <QtGui/QTextBlock>
#include <QtWidgets/QPlainTextEdit>
#include <functional>
#include <memory>

class CommittedPunctuation;
class CrumbHighlighter;
class QMenu;
class ViSelection;

// All document positions are UTF-16 offsets. A virtual row is never a block in
// the document and never appears in a clipboard, save, search or word count.
class ManuscriptEditor final : public QPlainTextEdit {
public:
    explicit ManuscriptEditor(QWidget *parent = nullptr);
    ~ManuscriptEditor() override;

    CrumbModel &model() const { return *model_; }
    QString manuscriptText() const;
    bool isLoadingText() const { return loadingText_; }
    QColor paperAtCaret() const;
    void loadText(const QString &text);
    void openAtEnd();
    void setShowManuscriptMarkup(bool show);
    bool showManuscriptMarkup() const { return showManuscriptMarkup_; }
    void setPaperPalette(const QPalette &palette);
    void setPaperActive(bool active);
    void setSmartPunctuation(bool enabled);
    void setLiteralGuillemets(bool enabled);
    bool literalGuillemets() const { return literalGuillemets_; }
    std::function<void()> onLiteralGuillemetsChanged;
    void toggleGuillemets();
    void setCopyDeletedText(bool enabled);
    bool copiesDeletedText() const { return copyDeleted_; }
    void copyRemovedText(const QString &text);
    std::function<void()> onTypedText;
    void finishTyping();
    void prepareSearch();
    void clearEditorSelection();
    void startViSelection();
    bool viSelectionActive() const;
    std::function<void(const QString &)> onViPrompt;
    std::function<void()> onViAccepted;

    void jumpTo(int position, bool center = true);
    void selectRange(int anchor, int position, bool center = true);
    void selectMatch(int start, int end);
    void setSearchHighlights(const QList<QTextEdit::ExtraSelection> &ranges);
    void setSpellingHighlights(const QList<QTextEdit::ExtraSelection> &ranges);
    void setSynonymHighlights(const QList<QTextEdit::ExtraSelection> &ranges);
    std::function<void(QMenu *, int)> spellingMenu;
    void centerCurrentLine();
    void topCurrentCrumb();
    void appendCrumb(const QString &genre = {});
    void continueList();
    void copyCrumb();
    void copyHeader();
    void deleteCrumb();
    void deleteWord();
    void insertLiteral(const QString &text);

    int virtualRows() const { return rows_; }
    int characterUnderPointer(const QPoint &point) const;
    void clearVirtual();
    std::function<void()> onVirtualChanged;

    static QString normalizeInsertion(QString text, bool keepFinalSpace = false);
    int replaceRangeLiteral(int start, int end, const QString &text, bool copyRemoval = true);
    void undo();
    void redo();
    void cut();
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

protected:
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    friend class ViSelection;
    enum class Alignment { Visible, Center, Top };
    enum class Input { Typed, Paste, Erase, Replacement };
    enum class EditRun { None, Typing, Backspace, Delete };
    class AppendUndoItem;
    QString removedSelection(const QTextCursor &cursor) const;
    int clampPosition(int value) const;
    QTextCursor at(int position) const;
    QTextCursor writingEnd() const;
    QTextBlock crumbEdgeBlock() const;
    void navigate(bool backward, bool fields, bool extend = false);
    void moveTo(int position, bool extend);
    bool moveByControlKey(int key, bool extend);
    void copyField(bool header);
    void onCursorMoved();
    void trimPending(bool joinPrevious = true);
    void replayEdit(bool forward);
    void trackSeparator(const QTextCursor &cursor);
    void typeText(const QString &text, int replaceStart = 0, int replaceLength = 0);
    bool typeListItem(QTextCursor &cursor, const QString &text);
    void insertListPrefix(QTextCursor &cursor, const QString &prefix);
    void showPreedit(QInputMethodEvent *event);
    void removeByKey(bool backward, bool byWord);
    void beginInputBlock(QTextCursor &cursor, EditRun run);
    void eraseSelection(QTextCursor cursor, EditRun run = EditRun::None);
    void insertSelection(const QString &text, Input kind);
    bool replaceAt(QTextCursor &cursor, const QString &text, Input kind);
    void recordEditForHash(QTextCursor &cursor, const QString &text);
    void applyPaper();
    void refreshBand();
    void setVirtualRows(int rows);
    bool onLastVisualRow() const;
    int rowPitch() const;
    void align(Alignment mode);
    void finishOpeningViewport();
    void clearAlignment();
    void setTopInset(int value);
    QRect virtualCaretRect() const;
    void paintCrumbBackground(QPainter &painter);

    std::unique_ptr<CrumbModel> model_;
    std::unique_ptr<CommittedPunctuation> punctuation_;
    std::unique_ptr<ViSelection> vi_;
    EditRun editRun_ = EditRun::None;
    CrumbHighlighter *highlighter_ = nullptr;
    QList<QTextEdit::ExtraSelection> searchHighlights_, spellingHighlights_;
    QList<QTextEdit::ExtraSelection> synonymHighlights_;
    AppendUndoItem *replayedAppend_ = nullptr;
    QPalette paper_;
    bool activePaper_ = true;
    bool loadingText_ = false;
    bool smart_ = true;
    bool showManuscriptMarkup_ = false;
    bool literalGuillemets_ = false;
    bool copyDeleted_ = false;
    bool editing_ = false;
    bool aligning_ = false;
    bool replaying_ = false;
    int activeLine_ = -1;
    QTextCursor pendingSpace_;
    QTextBlock declinedListParagraph_;
    int rows_ = 0;
    int virtualTop_ = 0;
    int topInset_ = 0;
    QSize outerSize_;
    bool alignQueued_ = false;
    Alignment alignment_ = Alignment::Visible;
};
#endif
