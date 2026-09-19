#include "layershellmenu.h"

#include "popup.h"

LayerShellMenu::LayerShellMenu()
    : QMenu(nullptr)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setStyleSheet(QStringLiteral(
        "QMenu { background: #303030; color: #eeeeee; border: 1px solid #606060; "
        "padding: 4px; }"
        "QMenu::item { padding: 5px 30px 5px 8px; }"
        "QMenu::item:selected { background: rgba(255, 255, 255, 32); }"
        "QMenu::item:disabled { color: #888888; }"
        "QMenu::separator { height: 1px; background: #606060; margin: 4px 6px; }"));
    setSeparatorsCollapsible(false);
}

bool LayerShellMenu::popupFor(QWidget *anchor)
{
    if (!anchor)
        return false;

    adjustSize();
    if (!LayerShellPopup::configure(this, anchor, "menu"))
        return false;

    // QMenu::popup() establishes Qt's popup grab and focus handling. The
    // Wayland platform uses the positioner configured above for placement.
    popup(QPoint(0, 0));
    return true;
}
