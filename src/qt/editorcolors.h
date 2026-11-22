#pragma once

#include <QPalette>

namespace EditorColors {

inline bool darkPaper(const QColor &paper)
{
    return qGray(paper.rgb()) < 128;
}

// Palette roles and painting both use the resulting opaque color.
inline QColor blend(const QColor &base, const QColor &overlay, int opacity)
{
    const auto channel = [opacity](int from, int to) {
        return (from * (255 - opacity) + to * opacity + 127) / 255;
    };
    return QColor(channel(base.red(), overlay.red()), channel(base.green(), overlay.green()),
                  channel(base.blue(), overlay.blue()));
}

inline QColor currentPaper(const QColor &base)
{
    return darkPaper(base) ? blend(base, Qt::white, 18) : blend(base, QColor(64, 140, 255), 31);
}

inline QColor inactivePaper(const QColor &base)
{
    return darkPaper(base) ? blend(base, Qt::white, 10) : blend(base, Qt::black, 26);
}

inline QColor warning(const QColor &background)
{
    return darkPaper(background) ? QColor(255, 138, 147) : QColor(171, 22, 50);
}

inline QColor synonymUnderline(const QPalette &palette)
{
    return darkPaper(palette.base().color()) ? QColor(126, 168, 210) : QColor(82, 118, 159);
}

inline QColor completionHint(const QPalette &palette)
{
    return blend(palette.base().color(), palette.text().color(), 180);
}

inline QColor searchPaper(const QPalette &palette)
{
    return darkPaper(palette.base().color()) ? QColor(78, 60, 24) : QColor(255, 224, 137);
}

// An explicit light/dark choice can be opposite to the desktop. Set related
// foreground/background pairs together, including the colors used by dialogs.
inline QPalette explicitTheme(QPalette palette, bool dark)
{
    const QColor window(dark ? "#2b2d30" : "#f5f5f5");
    const QColor paper(dark ? "#202124" : "#ffffff");
    const QColor text(dark ? "#f2f2f2" : "#242424");
    const QColor selection(dark ? "#406895" : "#3b70ac");
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, paper);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(dark ? "#b1b2b5" : "#60666b"));
    palette.setColor(QPalette::ToolTipBase, QColor(dark ? "#35383c" : "#fff7df"));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Link, QColor(dark ? "#9ec5fa" : "#255b99"));
    palette.setColor(QPalette::LinkVisited, QColor(dark ? "#d8aef1" : "#783e92"));
    palette.setColor(QPalette::Light, QColor(dark ? "#585b60" : "#ffffff"));
    palette.setColor(QPalette::Midlight, QColor(dark ? "#44474b" : "#e5e5e5"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#393b3f" : "#b8b8b8"));
    palette.setColor(QPalette::Dark, QColor(dark ? "#1a1b1e" : "#858585"));
    palette.setColor(QPalette::Shadow, QColor(dark ? "#101113" : "#565656"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, selection);
#endif
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(dark ? "#95979a" : "#737678"));
    return palette;
}

} // namespace EditorColors
