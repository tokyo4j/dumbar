#pragma once

#include <Qt>

class QWidget;

namespace LayerShellPopup
{
bool configure(QWidget *popup,
               QWidget *anchor,
               Qt::Edges anchorEdges = Qt::BottomEdge | Qt::RightEdge,
               Qt::Edges gravityEdges = Qt::BottomEdge | Qt::LeftEdge);
}
