#pragma once

#include "audio.h"
#include "battery.h"
#include "clock.h"
#include "taskbar.h"
#include "tray.h"

#include <gdkmm/monitor.h>
#include <gtkmm/box.h>
#include <gtkmm/window.h>

class IconResolver;
class StatusNotifierHost;
class ToplevelManager;

// A layer-shell bar at the top of one monitor.
class Panel final : public Gtk::Window
{
public:
    Panel(const Glib::RefPtr<Gdk::Monitor> &monitor,
          ToplevelManager &toplevels,
          IconResolver &icons,
          StatusNotifierHost &statusNotifierHost,
          AudioBackend &audioBackend);

private:
    Gtk::Box m_box;
    TaskBar m_taskbar;
    Tray m_tray;
    VolumeButton m_microphone;
    VolumeButton m_speaker;
    Battery m_battery;
    Clock m_clock;
};
