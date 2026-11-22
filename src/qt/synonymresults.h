#pragma once

#include <QDialog>
#include <QPointer>
#include <QTimer>
#include <functional>

class QLabel;
class QPushButton;
class QTableView;
class QTextDocument;
class SynonymMatcher;
class SynonymMatchModel;

// The result list belongs to one manuscript. Rows use UTF-16 manuscript
// offsets derived from the shared paragraph cache.
class SynonymResults final : public QDialog {
public:
    SynonymResults(QTextDocument *document, SynonymMatcher *matcher, QWidget *parent);
    void showGroup(int group);
    void reset();
    std::function<void(int, int)> jumpToMatch;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void invalidate();
    void scan();
    void edited(int position, int removed, int added);
    void showCount();
    void activate(int row);
    QPointer<QTextDocument> document_;
    QPointer<SynonymMatcher> matcher_;
    SynonymMatchModel *model_;
    QTableView *view_;
    QLabel *status_;
    QPushButton *go_;
    QTimer timer_;
    int group_ = -1;
    int nextLine_ = 0;
    int lineCount_ = 0;
    bool current_ = false;
};
