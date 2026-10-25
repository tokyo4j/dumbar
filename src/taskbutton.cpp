#include "taskbutton.h"

#include "config.h"
#include "iconresolver.h"
#include "toplevelmanager.h"
#include "workspacemanager.h"

#include <giomm/simpleactiongroup.h>
#include <gtkmm/gestureclick.h>

#include <algorithm>
#include <format>

TaskButton::TaskButton(Toplevel &toplevel, WorkspaceManager &workspaces, IconResolver &icons)
    : m_toplevel(toplevel)
    , m_workspaces(workspaces)
    , m_icons(icons)
    , m_box(Gtk::Orientation::HORIZONTAL, 4)
    , m_workspaceMenu(Gio::Menu::create())
{
    add_css_class("task");
    set_focus_on_click(false);

    m_icon.set_pixel_size(Config::kIconSize);
    m_title.set_ellipsize(Pango::EllipsizeMode::END);
    m_title.set_xalign(0);
    m_title.set_hexpand(true);
    m_box.append(m_icon);
    m_box.append(m_title);
    set_child(m_box);

    signal_clicked().connect([this] {
        if (m_toplevel.activated())
            m_toplevel.setMinimized(true);
        else
            m_toplevel.activate();
    });

    // Gtk::Button only handles the primary button.
    const auto click = Gtk::GestureClick::create();
    click->set_button(0);
    click->signal_pressed().connect([this, click = click.get()](int, double x, double y) {
        pressed(click->get_current_button(), x, y);
    });
    add_controller(click);

    const auto actions = Gio::SimpleActionGroup::create();
    m_minimizeAction = actions->add_action_bool("minimize", [this] {
        m_toplevel.setMinimized(!m_toplevel.minimized());
    });
    m_maximizeAction = actions->add_action_bool("maximize", [this] {
        m_toplevel.setMaximized(!m_toplevel.maximized());
    });
    m_fullscreenAction = actions->add_action_bool("fullscreen", [this] {
        m_toplevel.setFullscreen(!m_toplevel.fullscreen());
    });
    m_fullscreenAction->set_enabled(m_toplevel.supportsFullscreen());
    m_stickyAction = actions->add_action_bool("sticky", [this] {
        m_toplevel.setSticky(!m_toplevel.sticky());
    });
    m_workspaceAction = actions->add_action_radio_integer("workspace", [this](int index) {
        // Workspaces may have been removed while the menu was open.
        const auto &workspaces = m_workspaces.workspaces();
        if (size_t(index) < workspaces.size())
            m_toplevel.moveToWorkspace(*workspaces[index]);
    }, -1);
    actions->add_action("close", [this] { m_toplevel.close(); });
    insert_action_group("task", actions);

    const auto menu = Gio::Menu::create();
    const auto states = Gio::Menu::create();
    states->append("Minimize", "task.minimize");
    states->append("Maximize", "task.maximize");
    states->append("Fullscreen", "task.fullscreen");
    states->append("Sticky", "task.sticky");
    menu->append_section(states);
    menu->append_submenu("Move to Workspace", m_workspaceMenu);
    menu->append("Close", "task.close");
    m_menu.set_menu_model(menu);
    m_menu.set_has_arrow(false);
    m_menu.set_parent(*this);

    refresh();
}

TaskButton::~TaskButton()
{
    m_menu.unparent();
}

void TaskButton::refresh()
{
    if (!m_toplevel.iconName().empty())
        m_icon.set_from_icon_name(m_toplevel.iconName());
    else if (m_toplevel.iconTexture())
        m_icon.set(m_toplevel.iconTexture());
    else
        m_icon.set(m_icons.iconForAppId(m_toplevel.appId()));
    m_title.set_text(m_toplevel.title());
    set_tooltip_text(m_toplevel.title());
    if (m_toplevel.activated() && !m_toplevel.minimized())
        add_css_class("active");
    else
        remove_css_class("active");

    m_minimizeAction->set_state(Glib::Variant<bool>::create(m_toplevel.minimized()));
    m_maximizeAction->set_state(Glib::Variant<bool>::create(m_toplevel.maximized()));
    m_fullscreenAction->set_state(Glib::Variant<bool>::create(m_toplevel.fullscreen()));
    m_stickyAction->set_state(Glib::Variant<bool>::create(m_toplevel.sticky()));
    m_stickyAction->set_enabled(m_toplevel.supportsXfce());
    // The compositor does not move sticky windows.
    m_workspaceAction->set_enabled(m_toplevel.supportsXfce() && !m_toplevel.sticky());
}

void TaskButton::updateWorkspaceMenu()
{
    const auto &workspaces = m_workspaces.workspaces();
    const auto it = std::ranges::find(workspaces, m_toplevel.workspace(), &std::unique_ptr<Workspace>::get);
    m_workspaceAction->set_state(Glib::Variant<int>::create(it == workspaces.end() ? -1 : it - workspaces.begin()));

    m_workspaceMenu->remove_all();
    for (size_t i = 0; i < workspaces.size(); ++i)
        m_workspaceMenu->append(workspaces[i]->name(), std::format("task.workspace({})", i));
}

void TaskButton::pressed(guint button, double x, double y)
{
    switch (button) {
    case GDK_BUTTON_MIDDLE:
        m_toplevel.close();
        break;
    case GDK_BUTTON_SECONDARY:
        updateWorkspaceMenu();
        m_menu.set_pointing_to(Gdk::Rectangle(x, y, 1, 1));
        m_menu.popup();
        break;
    }
}
