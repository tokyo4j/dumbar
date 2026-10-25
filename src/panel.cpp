#include "panel.h"

#include "config.h"

#include <gdk/wayland/gdkwayland.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>

Panel::Panel(const Glib::RefPtr<Gdk::Monitor> &monitor,
             ToplevelManager &toplevels,
             IconResolver &icons,
             StatusNotifierHost &statusNotifierHost,
             AudioBackend &audioBackend)
    : m_box(Gtk::Orientation::HORIZONTAL, 4)
    , m_taskbar(toplevels, icons, gdk_wayland_monitor_get_wl_output(monitor->gobj()))
    , m_tray(statusNotifierHost)
    , m_microphone(audioBackend, AudioEndpoint::Microphone)
    , m_speaker(audioBackend, AudioEndpoint::Speaker)
{
    add_css_class("panel");
    set_title("dumbar");
    set_default_size(-1, Config::kPanelHeight);

    gtk_layer_init_for_window(gobj());
    gtk_layer_set_monitor(gobj(), monitor->gobj());
    gtk_layer_set_namespace(gobj(), "dumbar");
    gtk_layer_set_layer(gobj(), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_TOP, true);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_LEFT, true);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_RIGHT, true);
    gtk_layer_set_exclusive_zone(gobj(), Config::kPanelHeight);

    m_box.append(m_taskbar);
    m_box.append(m_tray);
    m_box.append(m_microphone);
    m_box.append(m_speaker);
    m_box.append(m_battery);
    m_box.append(m_clock);
    set_child(m_box);
}
