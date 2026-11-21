#pragma once

#include <QDialog>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QTableView;
class QTextDocument;
class SynonymMatchModel;

// The result list belongs to one manuscript. Rows use UTF-16 manuscript
// offsets and are invalidated as soon as its text changes.
class SynonymResults final : public QDialog {
public:
    SynonymResults(QTextDocument *document, QWidget *parent);
    ~SynonymResults() override;
    void showGroup(const QStringList &aliases);
    void reset();
    std::function<void(int, int)> jumpToMatch;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void invalidate();
    void search();
    void activate(int row);
    QPointer<QTextDocument> document_;
    QStringList aliases_;
    SynonymMatchModel *model_;
    QTableView *view_;
    QLabel *status_;
    QPushButton *go_;
    QTimer timer_;
    QThreadPool worker_;
    std::shared_ptr<std::atomic_bool> cancel_;
    bool current_ = false;
};
