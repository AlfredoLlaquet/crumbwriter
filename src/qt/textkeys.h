#pragma once

#include <QKeyEvent>

// AltGr may arrive as either GroupSwitch or Ctrl+Alt. On macOS, Option
// directly types many printable symbols, including on Spanish keyboards.
inline bool isComposedTextKey(const QKeyEvent &event)
{
    if (event.text().isEmpty() || event.text().front().unicode() < 0x20) return false;
    const auto mods = event.modifiers() & ~Qt::KeypadModifier;
    if (mods.testFlag(Qt::GroupSwitchModifier)) return true;
#ifdef Q_OS_MACOS
    return (mods & ~(Qt::AltModifier | Qt::ShiftModifier)) == Qt::NoModifier &&
           mods.testFlag(Qt::AltModifier);
#else
    return mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::AltModifier) &&
           !mods.testFlag(Qt::MetaModifier);
#endif
}
