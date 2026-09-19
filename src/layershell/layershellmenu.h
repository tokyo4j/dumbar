#pragma once

#include <QMenu>

class QWidget;

class LayerShellMenu final : public QMenu
{
public:
    LayerShellMenu();

    bool popupFor(QWidget *anchor);
};
