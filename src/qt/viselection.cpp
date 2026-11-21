#include "viselection.h"
#include "manuscripteditor.h"
#include "vimotion.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextBoundaryFinder>

void ViSelection::prompt(const QString &hint) const
{
    if (!editor_.onViPrompt) return;
    QString message;
    if (active_) {
        QString display = command_ + preedit_;
        display.replace(u'\n', QChar(0x21b5));
        message = QObject::tr("Vi selection   %1").arg(display);
        if (!hint.isEmpty()) message += QStringLiteral("   ·   ") + hint;
    }
    editor_.onViPrompt(message);
}

void ViSelection::start()
{
    active_ = true;
    command_.clear();
    preedit_.clear();
    prompt(QObject::tr("Type a movement. Esc cancels."));
}

void ViSelection::cancel()
{
    if (!active_) return;
    active_ = false;
    command_.clear();
    preedit_.clear();
    prompt();
}

void ViSelection::reset()
{
    cancel();
    lastFind_.clear();
    lastTarget_.clear();
    lastLanding_ = -1;
}

void ViSelection::key(const QKeyEvent &event)
{
    const int key = event.key();
    const auto mods = event.modifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Escape) { cancel(); return; }
    if (key == Qt::Key_Backspace) {
        QTextBoundaryFinder chars(QTextBoundaryFinder::Grapheme, command_);
        chars.toEnd();
        command_.truncate(qMax(qsizetype(0), chars.toPreviousBoundary()));
        preedit_.clear();
        prompt();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        evaluate(true);
        return;
    }
    if (event.matches(QKeySequence::Paste)) {
        command_ += QApplication::clipboard()->text();
    } else if (!event.text().isEmpty() && event.text().front().unicode() >= 0x20 &&
               (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) ||
                mods.testFlag(Qt::GroupSwitchModifier) ||
                (mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::AltModifier)))) {
        command_ += event.text();
    } else {
        return;
    }
    evaluate();
}

void ViSelection::inputMethod(const QInputMethodEvent &event)
{
    preedit_ = event.preeditString();
    if (event.commitString().isEmpty() && !event.replacementLength()) {
        prompt();
        return;
    }
    const auto start = qBound(qsizetype(0), command_.size() + event.replacementStart(), command_.size());
    command_.replace(start, event.replacementLength(), event.commitString());
    evaluate();
}

void ViSelection::evaluate(bool enter)
{
    ViMotion motion;
    qsizetype pos = 0;
    if (!command_.isEmpty() && command_.front() >= u'1' && command_.front() <= u'9') {
        while (pos < command_.size() && command_[pos] >= u'0' && command_[pos] <= u'9') ++pos;
        bool ok;
        motion.count = QStringView(command_).left(pos).toInt(&ok);
        if (!ok) { prompt(QObject::tr("Count is too large. Backspace to edit.")); return; }
        motion.explicitCount = true;
    }
    if (pos == command_.size()) { prompt(QObject::tr("Type a movement")); return; }
    const QChar op = command_[pos++];
    motion.name = op;
    if (op == u'g') {
        if (pos == command_.size()) { prompt(QObject::tr("Type g, j, or k")); return; }
        motion.name += command_[pos++];
    }
    const bool phrase = op.toLower() == u'q';
    const bool findChar = QStringLiteral("fFtT").contains(op);
    const bool repeat = op == u';' || op == u',';
    if (phrase || findChar) {
        motion.target = command_.mid(pos);
        if (phrase && !enter) { prompt(QObject::tr("Enter selects the phrase")); return; }
        if (motion.target.isEmpty()) { prompt(phrase ? QObject::tr("Type a phrase") : QObject::tr("Type a character")); return; }
        if (findChar) {
            QTextBoundaryFinder chars(QTextBoundaryFinder::Grapheme, motion.target);
            if (chars.toNextBoundary() != motion.target.size()) {
                prompt(QObject::tr("Use one character. Backspace to edit."));
                return;
            }
        }
    } else {
        const bool valid = op == u'g' ? QStringLiteral("gjk").contains(motion.name.back())
                                      : QStringLiteral("hlwWbBeEjk0^$G;,").contains(op);
        if (!valid || pos != command_.size()) {
            prompt(QObject::tr("Unknown movement. Backspace to edit, or Esc."));
            return;
        }
    }
    if (repeat) {
        if (lastFind_.isEmpty()) { prompt(QObject::tr("No previous character find")); return; }
        motion.name = lastFind_;
        if (op == u',') motion.name = lastFind_.isUpper() ? lastFind_.toLower() : lastFind_.toUpper();
        motion.target = lastTarget_;
        motion.skipLastCharacter = lastLanding_ == editor_.textCursor().position() &&
                                   motion.name.front().isUpper() == landedAfterTarget_;
    }

    const auto cursor = editor_.textCursor();
    const auto dest = motion.destination(cursor);
    if (!dest) {
        prompt(findChar || repeat || phrase ? QObject::tr("Match not found. Backspace to edit, or Esc.")
                                           : QObject::tr("End of movement range. Backspace to edit, or Esc."));
        return;
    }
    if (findChar) {
        lastFind_ = motion.name;
        lastTarget_ = motion.target;
    }
    lastLanding_ = findChar || repeat ? *dest : -1;
    landedAfterTarget_ = motion.name == "f" || motion.name == "T";
    cancel();
    editor_.selectRange(cursor.position(), *dest, false);
}
