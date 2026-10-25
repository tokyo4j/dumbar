#include "tray.h"

#include "config.h"
#include "statusnotifierhost.h"
#include "trayitem.h"

#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gestureclick.h>

#include <algorithm>
#include <cassert>
#include <string_view>

namespace
{
// Converts the children of a com.canonical.dbusmenu layout node, (ia{sv}av),
// into menu items. Leaf items activate "dbusmenu.click" with their ID.
void appendMenuItems(const Glib::RefPtr<Gio::Menu> &menu, GVariant *layout)
{
    // Separators split the menu into sections.
    auto section = Gio::Menu::create();
    GVariantIter *children;
    g_variant_get(layout, "(ia{sv}av)", nullptr, nullptr, &children);
    GVariant *child;
    while (g_variant_iter_next(children, "v", &child)) {
        gint32 id;
        GVariant *properties;
        GVariant *grandchildren;
        g_variant_get(child, "(i@a{sv}@av)", &id, &properties, &grandchildren);
        const char *type = "";
        const char *label = "";
        g_variant_lookup(properties, "type", "&s", &type);
        g_variant_lookup(properties, "label", "&s", &label);

        if (type == std::string_view("separator")) {
            menu->append_section(section);
            section = Gio::Menu::create();
        } else if (g_variant_n_children(grandchildren) > 0) {
            const auto submenu = Gio::Menu::create();
            appendMenuItems(submenu, child);
            section->append_submenu(label, submenu);
        } else {
            const auto menuItem = Gio::MenuItem::create(label, "");
            menuItem->set_action_and_target("dbusmenu.click", Glib::Variant<gint32>::create(id));
            section->append_item(menuItem);
        }

        g_variant_unref(grandchildren);
        g_variant_unref(properties);
        g_variant_unref(child);
    }
    g_variant_iter_free(children);
    menu->append_section(section);
}
}

TrayButton::TrayButton(TrayItem &item)
    : m_item(item)
{
    set_focus_on_click(false);
    m_icon.set_pixel_size(Config::kIconSize);
    set_child(m_icon);

    signal_clicked().connect([this] { pressed(GDK_BUTTON_PRIMARY); });
    // Gtk::Button only handles the primary button.
    const auto click = Gtk::GestureClick::create();
    click->set_button(0);
    click->signal_pressed().connect([this, click = click.get()](int, double, double) {
        if (click->get_current_button() != GDK_BUTTON_PRIMARY)
            pressed(click->get_current_button());
    });
    add_controller(click);

    const auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL | Gtk::EventControllerScroll::Flags::DISCRETE);
    scroll->signal_scroll().connect([this](double, double dy) {
        m_item.scroll(static_cast<int>(-dy));
        return true;
    }, false);
    add_controller(scroll);

    const auto actions = Gio::SimpleActionGroup::create();
    actions->add_action_with_parameter("click", Glib::VARIANT_TYPE_INT32, [this](const Glib::VariantBase &id) {
        m_item.menuItemClicked(Glib::VariantBase::cast_dynamic<Glib::Variant<gint32>>(id).get());
    });
    insert_action_group("dbusmenu", actions);

    // Keep the parent menu visible while a submenu is open.
    m_menu.set_flags(Gtk::PopoverMenu::Flags::NESTED);
    m_menu.set_has_arrow(false);
    m_menu.set_parent(*this);

    m_item.changed.connect(sigc::mem_fun(*this, &TrayButton::refresh));
    refresh();
}

TrayButton::~TrayButton()
{
    m_menu.unparent();
}

void TrayButton::refresh()
{
    if (m_item.iconTexture())
        m_icon.set(m_item.iconTexture());
    else
        m_icon.set_from_icon_name(m_item.iconName());
    set_tooltip_text(m_item.tooltip());
    set_visible(!m_item.passive());
}

void TrayButton::pressed(guint button)
{
    const bool menuButton = button == GDK_BUTTON_SECONDARY || (button == GDK_BUTTON_PRIMARY && m_item.itemIsMenu());
    if (menuButton && m_item.hasMenu())
        showMenu();
    else if (button == GDK_BUTTON_PRIMARY)
        m_item.activate();
    else if (button == GDK_BUTTON_MIDDLE)
        m_item.secondaryActivate();
    else if (button == GDK_BUTTON_SECONDARY)
        m_item.contextMenu();
}

void TrayButton::showMenu()
{
    // Fetch the layout on every open so that dynamic menus are current.
    Glib::VariantBase layout;
    try {
        layout = m_item.menuLayout();
    } catch (const Glib::Error &error) {
        g_warning("Could not load the menu of tray item %s: %s", m_item.id().c_str(), error.what());
        return;
    }

    const auto menu = Gio::Menu::create();
    appendMenuItems(menu, layout.gobj());
    m_menu.set_menu_model(menu);
    m_menu.popup();
}

Tray::Tray(StatusNotifierHost &host)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 1)
{
    host.itemAdded.connect(sigc::mem_fun(*this, &Tray::addItem));
    host.itemRemoved.connect(sigc::mem_fun(*this, &Tray::removeItem));
    for (const auto &item : host.items())
        addItem(*item);
    updateVisibility();
}

void Tray::addItem(TrayItem &item)
{
    append(*m_buttons.emplace_back(std::make_unique<TrayButton>(item)));
    item.changed.connect(sigc::mem_fun(*this, &Tray::updateVisibility));
    updateVisibility();
}

void Tray::removeItem(TrayItem &item)
{
    const auto it = std::ranges::find(m_buttons, &item, [](const auto &button) { return &button->item(); });
    assert(it != m_buttons.end());
    remove(**it);
    m_buttons.erase(it);
    updateVisibility();
}

void Tray::updateVisibility()
{
    set_visible(std::ranges::any_of(m_buttons, [](const auto &button) { return button->get_visible(); }));
}
