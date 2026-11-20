#ifndef CRUMBWRITER_BOOKMARKS_H
#define CRUMBWRITER_BOOKMARKS_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <functional>
#include <memory>

class QWidget;

// One controller per manuscript window. All callback line numbers start at one.
class Bookmarks final : public QObject {
public:
    Bookmarks(QWidget *owner, std::function<int()> currentLine,
              std::function<int()> lineCount, std::function<QString(int)> titleAtLine,
              std::function<void(int)> jumpToLine);
    ~Bookmarks() override;

    bool openManuscript(const QString &path, QString *error = nullptr);
    // Call after the manuscript save succeeds. Even if its sidecar copy fails,
    // the target becomes the active path. The previous bookmarks stay pending.
    bool saveAs(const QString &newPath, QString *error = nullptr);
    bool flush(QString *error = nullptr);
    bool hasPending() const;
    void addCurrent();
    void showWindow();
    void refresh(); // Coalesced title updates while the manager is visible.
    std::function<void()> onChanged; // A successful edit or pending-state change.

private:
    class Dialogs;
    std::unique_ptr<Dialogs> dialogs_;
};

#endif
