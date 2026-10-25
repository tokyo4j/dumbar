#pragma once

namespace Config
{
inline constexpr auto kIconTheme = "breeze-dark";

// Glib::DateTime::format() strings.
inline constexpr auto kClockFormat = "%H:%M";
inline constexpr auto kClockTooltipFormat = "%H:%M %p\n%Y/%m/%d (%a)";

// Volume change in percent per mouse wheel step.
inline constexpr double kVolumeScrollStep = 1.2;

inline constexpr int kDBusTimeout = 1000;

inline constexpr int kPanelHeight = 40;
inline constexpr int kIconSize = 32;
inline constexpr int kTaskButtonPreferredWidth = 400;

inline constexpr auto kStyleSheet = R"css(
window.panel {
    background: #202020;
    color: #eeeeee;
    font-size: 16px;
    /* Breeze's symbolic icons are not meant to be recolored by GTK. */
    -gtk-icon-style: regular;
}

window.panel button {
    border: none;
    border-radius: 0;
    box-shadow: none;
    background: none;
    color: #eeeeee;
    min-width: 0;
    min-height: 0;
    padding: 2px;
}

window.panel button:hover {
    background: rgba(255, 255, 255, 0.094);
}

window.panel button.task {
    padding: 0 4px 0 2px;
}

window.panel button.task.active {
    background: #2075ba;
}
)css";
}
