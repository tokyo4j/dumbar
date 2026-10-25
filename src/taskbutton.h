#pragma once

#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/popovermenu.h>

class IconResolver;
class Toplevel;
class WorkspaceManager;

class TaskButton final : public Gtk::Button
{
public:
    TaskButton(Toplevel &toplevel, WorkspaceManager &workspaces, IconResolver &icons);
    ~TaskButton() override;

    Toplevel &toplevel() const { return m_toplevel; }

    void refresh();

private:
    void pressed(guint button, double x, double y);
    // Lists the workspaces to move the window to, as the menu pops up.
    void updateWorkspaceMenu();

    Toplevel &m_toplevel;
    WorkspaceManager &m_workspaces;
    IconResolver &m_icons;
    Gtk::Box m_box;
    Gtk::Image m_icon;
    Gtk::Label m_title;
    Gtk::PopoverMenu m_menu;
    Glib::RefPtr<Gio::Menu> m_workspaceMenu;
    // Check items in the menu, reflecting the window state.
    Glib::RefPtr<Gio::SimpleAction> m_minimizeAction;
    Glib::RefPtr<Gio::SimpleAction> m_maximizeAction;
    Glib::RefPtr<Gio::SimpleAction> m_fullscreenAction;
    Glib::RefPtr<Gio::SimpleAction> m_stickyAction;
    // The index of the window's workspace, or -1.
    Glib::RefPtr<Gio::SimpleAction> m_workspaceAction;
};
