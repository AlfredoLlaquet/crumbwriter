#pragma once

#include "manuscripteditor.h"
#include "vimotion.h"

class QInputMethodEvent;
class QInputMethodQueryEvent;
class QKeyEvent;

class ViSelection {
public:
    explicit ViSelection(ManuscriptEditor &editor) : editor_(editor) {}
    bool active() const { return active_; }
    void start();
    void cancel();
    void abandon();
    void reset();
    void key(const QKeyEvent &event);
    void inputMethod(const QInputMethodEvent &event);
    void inputMethodQuery(QInputMethodQueryEvent &event) const;

private:
    struct Origin {
        QTextCursor cursor;
        ManuscriptEditor::Alignment alignment;
        int verticalScroll, horizontalScroll, virtualRows, virtualTop, topInset;
    };
    void updatePreview();
    void restoreOrigin();
    void discardPreedit();
    void accept();
    void prompt(const QString &hint = {}) const;
    ManuscriptEditor &editor_;
    Origin origin_{};
    std::optional<ViMotion> preview_;
    QString command_, preedit_;
    QString lastFind_, lastTarget_;
    int lastLanding_ = -1;
    bool landedAfterTarget_ = false;
    bool active_ = false;
    bool discardingPreedit_ = false;
};
