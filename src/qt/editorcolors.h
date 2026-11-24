#pragma once

#include <QPalette>
#include <cmath>

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
inline constexpr QPalette::ColorRole accentRole = QPalette::Accent;
#else
inline constexpr QPalette::ColorRole accentRole = QPalette::Highlight;
#endif

inline QColor currentPaper(const QPalette &palette)
{
    const QColor base = palette.base().color();
    QColor accent = palette.color(accentRole);
    const int paperTone = qGray(base.rgb()), accentTone = qGray(accent.rgb());
    // On a nearly matching desktop accent keep a visible step in brightness.
    // Strongly colored accents retain their hue without becoming solid bands.
    if (darkPaper(base)) {
        if (accentTone < paperTone + 88)
            accent = blend(accent, Qt::white, (paperTone + 88 - accentTone) * 255 / (255 - accentTone));
        return blend(base, accent, 39);
    }
    if (accentTone > paperTone - 72)
        accent = blend(accent, Qt::black, (accentTone - paperTone + 72) * 255 / accentTone);
    return blend(base, accent, 30);
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
    const auto paper = currentPaper(palette);
    return blend(paper, palette.text().color(), darkPaper(paper) ? 107 : 138);
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
    const QColor accent = palette.color(QPalette::Active, accentRole);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    const QColor selection(dark ? "#406895" : "#3b70ac");
    const QColor selectedText(Qt::white);
#else
    // Older Qt uses Highlight for the desktop accent and the selection.
    const QColor selection = accent;
    const auto linear = [](double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
    const double luminance = 0.2126 * linear(accent.redF()) + 0.7152 * linear(accent.greenF()) +
                             0.0722 * linear(accent.blueF());
    const QColor selectedText(luminance > 0.179 ? Qt::black : Qt::white);
#endif
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, paper);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, selectedText);
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
    palette.setColor(QPalette::Accent, accent);
#endif
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(dark ? "#95979a" : "#737678"));
    return palette;
}

} // namespace EditorColors
