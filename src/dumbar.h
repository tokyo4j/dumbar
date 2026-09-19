#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcDumbar)

namespace DumbarStyle
{
inline constexpr auto kDefaultIconTheme = "breeze-dark";

inline constexpr auto kClockFormat = "HH:mm";
inline constexpr auto kClockTooltipFormat = "HH:mm AP\nyyyy/MM/dd (ddd)";

inline constexpr int kTooltipDelay = 500;

inline constexpr int kPanelHeight = 40;
inline constexpr int kPanelSpacing = 4;
inline constexpr int kPanelWidgetPadding = 2;
inline constexpr int kFontSize = 16;
inline constexpr int kIconSize = 32;
inline constexpr int kPanelButtonWidth = 36;

inline constexpr int kTaskButtonIconPadding = 2;
inline constexpr int kTaskButtonTextPadding = 4;
inline constexpr int kIconTextGap = 4;
inline constexpr int kTaskButtonPreferredWidth = 400;

inline constexpr int kDropIndicatorWidth = 2;
inline constexpr int kDropIndicatorHeight = 36;
inline constexpr int kDropIndicatorRadius = 1;
inline constexpr int kDragChipSize = 40;
inline constexpr int kDragChipRadius = 6;

inline constexpr int kTooltipMaxWidth = 700;
inline constexpr int kTooltipMargin = 5;
inline constexpr int kTooltipRadius = 3;

inline constexpr int kMenuPadding = 2;
inline constexpr int kMenuItemVerticalPadding = 3;
inline constexpr int kMenuItemRightPadding = 30;
inline constexpr int kMenuItemLeftPadding = 8;
inline constexpr int kMenuSeparatorHeight = 1;
inline constexpr int kMenuSeparatorVerticalMargin = 3;
inline constexpr int kMenuSeparatorHorizontalMargin = 6;

inline QString menuStyleSheet()
{
    return QStringLiteral(
               "QMenu { background: #303030; color: #eeeeee; font-size: %1px; border: 1px solid #606060; "
               "padding: %2px; }"
               "QMenu::item { padding: %3px %4px %3px %5px; }"
               "QMenu::item:selected { background: rgba(255, 255, 255, 32); }"
               "QMenu::item:disabled { color: #888888; }"
               "QMenu::separator { height: %6px; background: #606060; margin: %7px %8px; }")
        .arg(kFontSize)
        .arg(kMenuPadding)
        .arg(kMenuItemVerticalPadding)
        .arg(kMenuItemRightPadding)
        .arg(kMenuItemLeftPadding)
        .arg(kMenuSeparatorHeight)
        .arg(kMenuSeparatorVerticalMargin)
        .arg(kMenuSeparatorHorizontalMargin);
}

inline QString panelStyleSheet()
{
    return QStringLiteral(
               "QWidget#panel { background: #202020; color: #eeeeee; }"
               "QToolButton { border: none; padding: %1px; color: #eeeeee; }"
               "QToolButton:hover { background: rgba(255, 255, 255, 24); }"
               "QLabel { color: #eeeeee; }")
        .arg(kPanelWidgetPadding);
}

inline QString tooltipStyleSheet()
{
    return QStringLiteral(
        "QLabel#dumbarTooltip { background: #303030; color: #eeeeee; "
        "border: 1px solid #606060; border-radius: %1px; }")
        .arg(kTooltipRadius);
}

inline QString dropIndicatorStyleSheet()
{
    return QStringLiteral("QWidget#dropIndicator { background: rgba(255, 255, 255, 210); border-radius: %1px; }")
        .arg(kDropIndicatorRadius);
}
}
