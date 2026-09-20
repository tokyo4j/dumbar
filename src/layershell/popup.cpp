#include "popup.h"

#include "../dumbar.h"

#include <QVariant>
#include <QWidget>
#include <QWindow>

namespace LayerShellPopup
{
bool configure(QWidget *popup,
               QWidget *anchor,
               const char *name,
               Qt::Edges anchorEdges,
               Qt::Edges gravityEdges)
{
    if (!popup || !anchor)
        return false;

    QWidget *panel = anchor->window();
    QWindow *panelWindow = panel ? panel->windowHandle() : nullptr;
    if (!panelWindow) {
        qWarning("dumbar: could not find the panel window for the layer-shell popup");
        return false;
    }

    popup->winId();
    QWindow *popupWindow = popup->windowHandle();
    if (!popupWindow) {
        qWarning("dumbar: could not create the layer-shell popup's Wayland window");
        return false;
    }

    // The panel is a layer-shell surface. Positioner coordinates therefore
    // have to be relative to the panel, rather than global screen coordinates.
    popupWindow->setTransientParent(panelWindow);
    QRect anchorRect(anchor->mapTo(panel, QPoint(0, 0)), anchor->size());
    anchorRect.adjust(0, -3, 0, 3);
    popupWindow->setProperty("_q_waylandPopupAnchorRect", QVariant::fromValue(anchorRect));
    popupWindow->setProperty("_q_waylandPopupAnchor", QVariant::fromValue(anchorEdges));
    popupWindow->setProperty("_q_waylandPopupGravity", QVariant::fromValue(gravityEdges));
    // xdg_positioner: slide_x | slide_y | flip_x | flip_y.
    popupWindow->setProperty("_q_waylandPopupConstraintAdjustment",
                            QVariant::fromValue(1u | 2u | 4u | 8u));

    qCDebug(lcDumbar) << "configured layer-shell popup"
                      << "name=" << name
                      << "popupWindow=" << static_cast<void *>(popupWindow)
                      << "parentWindow=" << static_cast<void *>(panelWindow)
                      << "anchor=" << anchorEdges
                      << "gravity=" << gravityEdges
                      << "anchorRect=" << anchorRect;
    return true;
}
}
