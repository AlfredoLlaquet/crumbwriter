#include "documentformats.h"
#include "mainwindow.h"
#include "manuscripteditor.h"
#include "preferences.h"
#include "spellwords.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QThread>
#include <QToolButton>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool ok, const QString &detail)
{
    if (!ok) { std::fprintf(stderr, "3.1 regression: %s\n", qPrintable(detail)); std::exit(1); }
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), path);
}

void key(QWidget *widget, int code, Qt::KeyboardModifiers modifiers = {}, const QString &text = {})
{
    QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
    QApplication::sendEvent(widget, &event);
    QApplication::processEvents();
}

bool receivesFocus(QWidget *widget)
{
    QElapsedTimer clock;
    clock.start();
    while (!widget->hasFocus() && clock.elapsed() < 5000) { QApplication::processEvents(); QThread::msleep(5); }
    return widget->hasFocus();
}

void leftClick(QWidget *widget, const QPoint &pos)
{
    // A real pointer travels from the popup trigger to its menu item.
    const QPoint start = widget->mapFromGlobal(QCursor::pos());
    for (int i = 1; i <= 8; ++i) {
        const QPoint next = start + (pos - start) * i / 8;
        QMouseEvent move(QEvent::MouseMove, next, widget->mapToGlobal(next), Qt::NoButton, {}, {});
        QApplication::sendEvent(widget, &move);
    }
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        auto buttons = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
        QMouseEvent event(type, pos, widget->mapToGlobal(pos), Qt::LeftButton, buttons, {});
        QApplication::sendEvent(widget, &event);
    }
    QApplication::processEvents();
}

void type(QWidget *widget, const QString &text)
{
    for (const auto c : text) key(widget, c.toUpper().unicode(), {}, QString(c));
}

void editing()
{
    ManuscriptEditor ed;
    ed.loadText("one two");
    ed.jumpTo(4, false);
    type(&ed, "  ");
    check(ed.manuscriptText() == "one   two", "inter-word spaces remain editable");
    ed.jumpTo(5, false);
    type(&ed, "and");
    check(ed.manuscriptText() == "one and two", ed.manuscriptText());

    ed.loadText(QStringLiteral("first—second"));
    ed.jumpTo(0, false);
    for (const int pos : {5, 6, 12}) {
        key(&ed, Qt::Key_Right, Qt::ControlModifier);
        check(ed.textCursor().position() == pos, "forward em-dash boundary");
    }
    for (const int pos : {6, 5, 0}) {
        key(&ed, Qt::Key_Left, Qt::ControlModifier);
        check(ed.textCursor().position() == pos, "backward em-dash boundary");
    }
    for (const auto &marker : {QString("*"), QString("-"), QString("(*)")}) {
        ed.loadText(marker + " one");
        ed.openAtEnd();
        key(&ed, Qt::Key_Return, {}, "\n");
        type(&ed, marker + " two");
        check(ed.manuscriptText() == marker + " one\n" + marker + " two", ed.manuscriptText());
        key(&ed, Qt::Key_Return, {}, "\n");
        QApplication::clipboard()->setText(marker + " three\n" + marker + " four");
        ed.paste();
        check(ed.manuscriptText().endsWith(marker + " two\n" + marker + " three\n" + marker + " four"), ed.manuscriptText());
    }
    ed.loadText("1. one");
    ed.openAtEnd();
    key(&ed, Qt::Key_Return, {}, "\n");
    QApplication::clipboard()->setText("8. eight\n9. nine");
    ed.paste();
    check(ed.manuscriptText() == "1. one\n8. eight\n9. nine", ed.manuscriptText());
}

void formats(const QDir &tmp)
{
    using namespace DocumentFormats;
    ManuscriptEditor ed;
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(17);
    ed.setFont(font);
    ed.loadText(QStringLiteral("A Book\n======\nDivision\n========\nAn «italic» word. Next.\n* one"));
    QString error;
    for (const auto &[kind, suffix] : {std::pair{Format::Docx, "docx"}, {Format::Odt, "odt"},
                                      {Format::Html, "html"}, {Format::Pdf, "pdf"}}) {
        const QString path = tmp.filePath(QStringLiteral("Book.") + suffix);
        check(exportFile(path, kind, {}, &ed, &error), QString(suffix) + ": " + error);
        auto text = importFile(path, kind, {}, &error);
        check(error.isEmpty() && text.contains("A Book") && text.contains("Division") && text.contains("* one"),
              QString(suffix) + ": " + error + " / " + text);
        if (kind == Format::Pdf)
            check(text.trimmed().endsWith("* one"), "PDF must contain only the manuscript text");
        else
            check(text.contains(QStringLiteral("Division\n========")) && text.contains(QStringLiteral("An «italic» word.")),
                  QString(suffix) + ": heading and emphasis / " + text);
    }
    auto path = tmp.filePath("proof.txt");
    TextOptions latin{"Windows-1252", 80};
    ed.loadText(QStringLiteral("Café. Next."));
    check(exportFile(path, Format::FixedText, latin, &ed, &error), error);
    check(importFile(path, Format::Text, latin, &error).trimmed() == QStringLiteral("Café.  Next."), error);
    ed.loadText(QStringLiteral("風"));
    check(!exportFile(path, Format::Text, latin, &ed, &error) && !error.isEmpty(), "unrepresentable character");
    check(importFile(path, Format::Text, latin, &error).startsWith(QStringLiteral("Café")), "destination preserved");
    writeFile(path, "One fine\nmorning.\n\n* one\n* two");
    check(importFile(path, Format::FixedText, {"UTF-8", 10}, &error) == "One fine morning.\n\n* one\n* two", error);
    writeFile(path, "# A Book\n\n*italic* text\n");
    check(importFile(path, Format::Markdown, {}, &error).contains(QStringLiteral("«italic» text")), error);
    writeFile(path, R"({\rtf1\ansi Caf\'e9 \i yes\i0 .\par End})");
    check(importFile(path, Format::Rtf, {}, &error) == QStringLiteral("Café «yes». \nEnd").replace(". \n", ".\n"), error);
}

void controls(const QTemporaryDir &tmp)
{
    Preferences prefs(tmp.filePath("ui.ini"));
    prefs.settings().setValue("editor/backup", false);
    prefs.settings().setValue("editor/live-save", false);
    const QString path = tmp.filePath("chapter.txt");
    writeFile(path, "A Book\nBody.\nLast.\n");
    writeFile(path + ".characters", "Mara, the clerk\nZoe\n");
    writeFile(path + ".recurring", "a star, A & B\n");
    QJsonObject state;
    {
        MainWindow w(prefs);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1200, 650);
        w.activateManuscript(false);
        check(w.loadDocument(path), path);
        auto *ed = w.editor();
        check(receivesFocus(ed), "the opened manuscript owns keyboard focus before testing controls");
        ed->jumpTo(0, false);
        auto *style = w.findChild<QComboBox *>("lineStyle");
        auto *characters = w.findChild<QListView *>("charactersList");
        check(style && style->count() == 2, "first-line style choices");
        check(characters && !characters->isVisible(), "Characters hidden in Generic mode");
        for (auto *action : w.findChildren<QAction *>()) {
            if (action->text() == "Characters") action->trigger();
        }
        check(characters->isVisible(), "Characters can be shown");
        const auto row = characters->model()->index(1, 0);
        check(row.isValid(), "second sidebar entry loaded");
        characters->customContextMenuRequested(characters->visualRect(row).center());
        auto *entryMenu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        check(entryMenu && characters->currentIndex() == row, "right-click targets the clicked row");
        for (auto *a : entryMenu->actions()) {
            if (a->text().contains("Insert at caret")) {
                check(a->isEnabled(), "sidebar insertion available");
                leftClick(entryMenu, entryMenu->actionGeometry(a).center());
                break;
            }
        }
        check(ed->manuscriptText().startsWith("Zoe"), "insert the selected entry at the manuscript caret");
        ed->undo();
        ed->jumpTo(0, false);
        auto popup = [] { return qobject_cast<QMenu *>(QApplication::activePopupWidget()); };
        auto clickRow = [](QListView *list, int index) {
            leftClick(list->viewport(), list->visualRect(list->model()->index(index, 0)).center());
        };
        ed->selectRange(0, 1, false);
        clickRow(characters, 0);
        check(popup() && popup()->actions().size() == 2 && popup()->activeAction()->text() == "Mara",
              "a single left-click offers both names in saved order");
        key(popup(), Qt::Key_Escape);
        check(ed->textCursor().selectedText() == "A" && ed->manuscriptText().startsWith("A Book"),
              "canceling preserves the original text and selection");
        check(receivesFocus(ed), "canceling restores keyboard focus to the manuscript");
        auto *recurring = w.findChild<QListView *>("recurringList");
        clickRow(recurring, 0);
        auto *choices = popup();
        check(choices && choices->actions().size() == 2, "recurring element shows its choices");
        const auto choicePos = choices->actionGeometry(choices->actions().last()).center();
        check(choices->actionAt(choicePos) == choices->actions().last(), "mouse addresses the alternate form");
        leftClick(choices, choicePos);
        check(ed->manuscriptText().startsWith("A & B Book"), "a mouse choice inserts the literal alias over the selection");
        ed->undo();
        ed->jumpTo(0, false);
        auto *menu = w.findChild<QToolButton *>("charactersMenu")->menu();
        auto *insert = [&] {
            for (auto *a : menu->actions()) if (a->text().contains("Insert at caret")) return a;
            return static_cast<QAction *>(nullptr);
        }();
        check(insert, "sidebar menu has Insert at caret");
        for (bool contextMenu : {false, true}) {
            if (contextMenu) characters->customContextMenuRequested(characters->visualRect(characters->model()->index(0, 0)).center());
            else menu->popup(characters->mapToGlobal(QPoint(0, 0)));
            leftClick(menu, menu->actionGeometry(insert).center());
            check(popup() && popup()->actions().first()->text() == "Mara", "both sidebar menus open name selection");
            key(popup(), Qt::Key_Down);
            key(popup(), Qt::Key_Return);
            check(ed->manuscriptText().startsWith("the clerkA Book"), "keyboard confirms the alternate name");
            ed->undo();
            ed->jumpTo(0, false);
        }
        auto setStyle = [&](int value) {
            const int i = style->findData(value);
            check(i >= 0, "requested style is coherent");
            style->activated(i);
            QApplication::processEvents();
        };
        setStyle(0);
        check(ed->model().paragraphRole(0) == ParagraphRole::Body, "explicit regular first line");
        setStyle(-1);
        check(ed->model().paragraphRole(0) == ParagraphRole::DocumentTitle, "main title");
        auto paragraph = ed->document()->find("Body.");
        ed->jumpTo(paragraph.selectionStart(), false);
        check(style->count() == 5, "later-line style choices");
        setStyle(3);
        check(ed->manuscriptText().contains("Body.\n~~~~~"), ed->manuscriptText());
        ed->setFocus();
        key(ed, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        key(ed, Qt::Key_2, {}, "2");
        check(ed->manuscriptText().contains("Body.\n-----"), "keyboard line style");
        check(w.findChild<QSpinBox *>("writingFontSize")->accessibleName() == "Font size", "unlabeled size is accessible");
        auto *family = w.findChild<QComboBox *>("writingFont");
        auto *size = w.findChild<QSpinBox *>("writingFontSize");
        auto labelFor = [&](QWidget *widget) -> QLabel * {
            for (auto *label : w.findChildren<QLabel *>()) if (label->buddy() == widget) return label;
            return nullptr;
        };
        auto gap = [&](QWidget *left, QWidget *right) {
            return right->mapTo(&w, QPoint(0, 0)).x() - left->mapTo(&w, QPoint(left->width(), 0)).x();
        };
        auto *fontLabel = labelFor(family);
        auto *styleLabel = labelFor(style);
        check(fontLabel && styleLabel && gap(fontLabel, family) >= 6 && gap(family, size) >= 6 &&
              gap(size, styleLabel) >= 12 && gap(styleLabel, style) >= 6, "font and style controls have visible spacing");
        w.findChild<QToolButton *>("addTab")->click();
        check(w.tabCount() == 2 && w.editor()->manuscriptText().isEmpty(), "+ opens a blank tab");
        auto *tabs = w.findChild<QTabWidget *>("manuscript");
        const auto point = QPointF(tabs->width() / 2, tabs->tabBar()->height() / 2);
        QMouseEvent twice(QEvent::MouseButtonDblClick, point, tabs->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(tabs, &twice);
        check(w.tabCount() == 3, "double-clicking the unused tab strip opens one tab");
        ed = w.editor();
        ed->loadText("A misspelld word and a misspelld word.");
        ed->jumpTo(5, false);
        QElapsedTimer clock;
        clock.start();
        while (SpellWords::loading() && clock.elapsed() < 10000) { QApplication::processEvents(); QThread::msleep(10); }
        key(ed, Qt::Key_Period, Qt::ControlModifier);
        auto *corrections = ed->findChild<QMenu *>("caretCorrections");
        check(corrections && corrections->isVisible(), "spelling corrections at the caret");
        auto *choice = corrections->actions().first();
        const auto replacement = choice->text();
        choice->trigger();
        corrections->hide();
        check(ed->manuscriptText().startsWith("A " + replacement) && ed->manuscriptText().endsWith("a misspelld word."),
              "one local spelling correction");
        ed->loadText({});
        state = w.workspaceState();
        check(state["tabs"].toArray().size() == 3, "all tabs recorded");
        auto *fileMenu = w.findChild<QMenuBar *>("manuscriptMenuBar")->actions().first()->menu();
        auto *newFile = fileMenu->actions().first();
        check(newFile->text().section('\t', 0, 0) == "New File", "File begins with New File");
        newFile->trigger();
        QApplication::processEvents();
        check(w.tabCount() == 4 && w.editor() != ed && w.path().isEmpty() && w.editor()->manuscriptText().isEmpty(),
              "File > New File opens a fresh untitled tab in the same window");
        check(receivesFocus(w.editor()), "File > New File returns keyboard focus to the new manuscript");
        for (auto *action : w.findChildren<QAction *>()) {
            if (action->text() == "Crumb Writer Help") action->trigger();
        }
        check(w.findChild<QDialog *>("helpBrowser") != nullptr, "integrated guide");
    }
    MainWindow reopened(prefs);
    reopened.setAttribute(Qt::WA_DeleteOnClose, false);
    check(reopened.restoreWorkspace(state).isEmpty() && reopened.tabCount() == 3 && reopened.path().isEmpty(),
          "restored order and active blank tab");
}
void restoredDraft(const QTemporaryDir &tmp)
{
    Preferences prefs(tmp.filePath("draft.ini"));
    prefs.settings().setValue("editor/live-save", false);
    QJsonObject state;
    QString draft;
    auto closeUsing = [&](MainWindow &window, QMessageBox::ButtonRole role) {
        window.close();
        auto *box = window.findChild<QMessageBox *>("confirmClose");
        check(box && box->isVisible(), "closing an edited draft offers a decision");
        for (auto *button : box->buttons()) {
            if (box->buttonRole(button) == role) { button->click(); break; }
        }
        QElapsedTimer clock;
        clock.start();
        while (window.isVisible() && clock.elapsed() < 10000) {
            QApplication::processEvents();
            QThread::msleep(5);
        }
        check(!window.isVisible(), "the close completed after persisting the decision");
    };
    {
        MainWindow window(prefs);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.workspaceChanged = [&](bool closing) { if (closing) state = window.workspaceState(); };
        window.activateManuscript(false);
        type(window.editor(), "My untitled draft.");
        window.editor()->jumpTo(5, false);
        closeUsing(window, QMessageBox::AcceptRole);
        const auto tabs = state.value("tabs").toArray();
        check(tabs.size() == 1, "one retained draft");
        draft = tabs.first().toObject().value("draft").toString();
        check(QFile::exists(draft), "draft was persisted with live save disabled");
    }
    MainWindow restored(prefs);
    restored.setAttribute(Qt::WA_DeleteOnClose, false);
    restored.workspaceChanged = [&](bool closing) { if (closing) state = restored.workspaceState(); };
    check(restored.restoreWorkspace(state).isEmpty() &&
          restored.editor()->manuscriptText().trimmed() == "My untitled draft." &&
          restored.editor()->textCursor().position() == 5, "draft text and caret recovered");
    type(restored.editor(), "new");
    closeUsing(restored, QMessageBox::DestructiveRole);
    check(!QFile::exists(draft) && state.value("tabs").toArray().isEmpty(), "discarded draft stays closed");
}

}

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    QTemporaryDir tmp;
    qputenv("XDG_DATA_HOME", QFile::encodeName(tmp.filePath("data")));
    QApplication app(argc, argv);
    check(tmp.isValid(), "temporary directory");
    if (app.arguments().size() == 3 && app.arguments().at(1) == "--export-samples") {
        const QDir samples(app.arguments().at(2));
        check(samples.mkpath("."), samples.path());
        formats(samples);
        return 0;
    }
    editing();
    formats(QDir(tmp.path()));
    controls(tmp);
    restoredDraft(tmp);
    return 0;
}
