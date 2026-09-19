#pragma once

#include <QWidget>

class Audio;
class Battery;
class Clock;
class IconResolver;
class TaskBar;
class ToplevelManager;
class Tray;
class QScreen;
struct wl_output;

class Panel final : public QWidget
{
    Q_OBJECT

public:
    Panel(ToplevelManager *toplevelManager,
          IconResolver *icons,
          QScreen *screen,
          QWidget *parent = nullptr);

private:
    void configureLayerShell();

    IconResolver *m_icons = nullptr;
    ToplevelManager *m_toplevelManager = nullptr;
    QScreen *m_screen = nullptr;
    wl_output *m_output = nullptr;
    TaskBar *m_taskbar = nullptr;
    Tray *m_tray = nullptr;
    Audio *m_audio = nullptr;
    Battery *m_battery = nullptr;
    Clock *m_clock = nullptr;
};
