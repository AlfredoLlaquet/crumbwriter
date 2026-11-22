#include "mainwindow.h"
#include "manuscripteditor.h"
#include "preferences.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <vector>

// Time the real window through the paint of the edited line, including Qt's
// resize and key handling. The compositor can present that frame later.
class PaintTimer final : public QApplication {
public:
    using QApplication::QApplication;

    void start(ManuscriptEditor *view, int position)
    {
        editor = view;
        expectedPosition = position;
        elapsedMs = -1;
        clock.start();
    }

    double waitForPaint()
    {
        while (elapsedMs < 0 && clock.elapsed() < 8000) {
            processEvents(QEventLoop::AllEvents, 20);
            if (elapsedMs < 0)
                QThread::msleep(1);
        }
        editor = nullptr;
        return elapsedMs;
    }

protected:
    bool notify(QObject *receiver, QEvent *event) override
    {
        const bool caretPaint = editor && receiver == editor->viewport()
            && event->type() == QEvent::Paint && editor->textCursor().position() == expectedPosition
            && static_cast<QPaintEvent *>(event)->region().intersects(editor->cursorRect());
        bool result = QApplication::notify(receiver, event);
        if (caretPaint && elapsedMs < 0)
            elapsedMs = double(clock.nsecsElapsed()) / 1000000;
        return result;
    }

private:
    ManuscriptEditor *editor = nullptr;
    int expectedPosition = -1;
    double elapsedMs = -1;
    QElapsedTimer clock;
};

static bool exercise(PaintTimer &app, const QString &directory, int count, bool generic = false)
{
    const QByteArray body = "Proportional text wraps across several visual lines. ";
    QByteArray original = generic ? QByteArray() : QByteArray("Resize//");
    for (int n = 1; n <= count; ++n) {
        if (n > 1)
            original += '\n';
        if (!generic)
            original += "Crumb " + QByteArray::number(n) + ": Genre #" + QByteArray::number(n) + ": ";
        original += body.repeated(3 + n % 4) + "End.";
    }
    QString path = directory + "/Manuscript-" + QString::number(count) + ".txt";
    QFile fixture(path);
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write(original) != original.size())
        return false;
    fixture.close();
    // Give every paragraph several visible, overlapping alias matches while
    // resizing and typing. A whole-document rescan here would scale with count.
    QFile names(path + ".characters");
    QFile terms(path + ".recurring");
    if (!names.open(QIODevice::WriteOnly) || names.write("Proportional text, text\n") < 0 ||
        !terms.open(QIODevice::WriteOnly) || terms.write("visual lines, lines\n") < 0)
        return false;
    names.close();
    terms.close();

    Preferences prefs(directory + "/settings.ini");
    prefs.settings().setValue("editor/backup", false);
    prefs.settings().setValue("editor/live-save", false);
    MainWindow window(prefs);
    if (!window.loadDocument(path))
        return false;
    ManuscriptEditor *editor = window.editor();
    window.resize(1050, 680);
    app.start(editor, int(original.size())); // This timed fixture uses ASCII.
    window.show();
    editor->setFocus();
    if (app.waitForPaint() < 0 || editor->textCursor().blockNumber() != count - 1)
        return false;

    std::vector<double> latency;
    auto wide = editor->viewport()->width();
    auto narrow = wide;
    for (int step = 0; step < 20; ++step) {
        int offset = step < 10 ? step : 19 - step;
        int width = 1030 - 40 * offset;
        app.start(editor, editor->textCursor().position() + 1);
        window.resize(width, 680 + (step % 3) * 12);
        QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QCoreApplication::sendEvent(editor, &typed);
        double ms = app.waitForPaint();
        if (ms < 0 || editor->textCursor().blockNumber() != count - 1)
            return false;
        narrow = std::min(narrow, editor->viewport()->width());
        latency.push_back(ms);
    }
    std::sort(latency.begin(), latency.end());
    std::printf("Qt %s: %d %s, manuscript width %d to %d px, resize and key to completed "
                "paint p95 %.1f ms, max %.1f ms (20 samples)\n",
                qVersion(), count, generic ? "paragraphs" : "crumbs", wide, narrow, latency[18], latency.back());
    return narrow < wide && editor->manuscriptText().toUtf8() == original + QByteArray(20, 'x');
}

int main(int argc, char **argv)
{
    PaintTimer app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid() || !exercise(app, directory.path(), 3371)
        || !exercise(app, directory.path(), 25092) || !exercise(app, directory.path(), 25092, true)) {
        std::fprintf(stderr, "Resizing failed to show and preserve typed text at the last crumb.\n");
        return 1;
    }
    return 0;
}
