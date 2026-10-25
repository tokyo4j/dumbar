#pragma once

#include <giomm/simpleaction.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/popovermenu.h>

class IconResolver;
class Toplevel;

class TaskButton final : public Gtk::Button
{
public:
    TaskButton(Toplevel &toplevel, IconResolver &icons);
    ~TaskButton() override;

    Toplevel &toplevel() const { return m_toplevel; }

    void refresh();

private:
    void pressed(guint button, double x, double y);

    Toplevel &m_toplevel;
    IconResolver &m_icons;
    Gtk::Box m_box;
    Gtk::Image m_icon;
    Gtk::Label m_title;
    Gtk::PopoverMenu m_menu;
    // Check items in the menu, reflecting the window state.
    Glib::RefPtr<Gio::SimpleAction> m_minimizeAction;
    Glib::RefPtr<Gio::SimpleAction> m_maximizeAction;
    Glib::RefPtr<Gio::SimpleAction> m_fullscreenAction;
};
