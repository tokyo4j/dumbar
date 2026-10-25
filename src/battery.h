#pragma once

#include <giomm/dbusproxy.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>

// Shows UPower's display device, which aggregates all batteries.
class Battery final : public Gtk::Box
{
public:
    Battery();

private:
    void refresh();

    Glib::RefPtr<Gio::DBus::Proxy> m_proxy;
    Gtk::Image m_icon;
    Gtk::Label m_label;
};
