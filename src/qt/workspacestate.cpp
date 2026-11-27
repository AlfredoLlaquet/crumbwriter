#include "mainwindow.h"
#include "documentstore.h"
#include "manuscripteditor.h"
#include "manuscriptsession.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>

QJsonObject MainWindow::workspaceState() const
{
    QJsonArray tabs;
    int active = 0;
    for (int i = 0; i < tabs_->count(); ++i) {
        for (auto *session : sessions_) {
            if (session->editor_ != tabs_->widget(i) || omittedDrafts_.contains(session)) continue;
            if (session == active_) active = tabs.size();
            const auto caret = session->editor_->textCursor();
            tabs.append(QJsonObject{{"path", session->path()}, {"draft", session->store_->draftPath()},
                                    {"anchor", caret.anchor()}, {"caret", caret.position()},
                                    {"scroll", session->editor_->verticalScrollBar()->value()}});
        }
    }
    return {{"tabs", tabs}, {"active", active}};
}

QStringList MainWindow::restoreWorkspace(const QJsonObject &state)
{
    QStringList missing;
    ManuscriptSession *selected = nullptr;
    const auto tabs = state.value("tabs").toArray();
    int restored = 0;
    for (int i = 0; i < tabs.size(); ++i) {
        const auto tab = tabs.at(i).toObject();
        const QString path = tab.value("path").toString();
        const QString draft = tab.value("draft").toString();
        const QString file = path.isEmpty() ? draft : path;
        if (!file.isEmpty() && (!QFileInfo::exists(file) || findOpen(file))) {
            if (!QFileInfo::exists(file)) missing.append(file);
            continue;
        }
        if (restored) newTab();
        if (!file.isEmpty() && !active_->load(file, path.isEmpty())) {
            missing.append(file);
            if (restored) removeSession(active_);
            continue;
        }
        ++restored;
        active_->openingCaret_ = -1;
        active_->editor_->selectRange(tab.value("anchor").toInt(), tab.value("caret").toInt(), false);
        auto *editor = active_->editor_;
        const int scroll = tab.value("scroll").toInt();
        QTimer::singleShot(0, editor, [editor, scroll] { editor->verticalScrollBar()->setValue(scroll); });
        if (i == state.value("active").toInt()) selected = active_;
    }
    if (selected) selectSession(selected);
    activateManuscript(false);
    return missing;
}
