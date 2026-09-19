#include "layershellmenu.h"

#include "../dumbar.h"
#include "popup.h"

#include <QFont>

LayerShellMenu::LayerShellMenu()
    : QMenu(nullptr)
{
    QFont menuFont = font();
    menuFont.setPixelSize(DumbarStyle::kFontSize);
    setFont(menuFont);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setStyleSheet(DumbarStyle::menuStyleSheet());
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
