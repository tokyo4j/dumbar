#include "trayitem.h"

#include "config.h"

#include <gdkmm/display.h>
#include <gdkmm/memorytexture.h>
#include <gtkmm/icontheme.h>

#include <climits>
#include <cstdlib>

namespace
{
constexpr auto kItemInterface = "org.kde.StatusNotifierItem";
constexpr auto kMenuInterface = "com.canonical.dbusmenu";

// Looks up a string-like ("&s" or "&o") entry of an a{sv} dictionary.
std::string lookupString(GVariant *properties, const char *name, const char *format = "&s")
{
    const char *value = nullptr;
    g_variant_lookup(properties, name, format, &value);
    return value ? value : "";
}

// Picks the pixmap closest to the icon size from an a(iiay) array of ARGB32
// images in network byte order.
Glib::RefPtr<Gdk::Texture> lookupPixmap(GVariant *properties, const char *name)
{
    GVariantIter *pixmaps;
    if (!g_variant_lookup(properties, name, "a(iiay)", &pixmaps))
        return {};

    Glib::RefPtr<Gdk::Texture> best;
    int bestScore = INT_MAX;
    gint32 width, height;
    GVariant *pixels;
    while (g_variant_iter_next(pixmaps, "(ii@ay)", &width, &height, &pixels)) {
        const int score = std::abs(width - Config::kIconSize) + std::abs(height - Config::kIconSize);
        gsize size;
        const void *data = g_variant_get_fixed_array(pixels, &size, 1);
        if (width > 0 && height > 0 && size >= gsize(width) * height * 4 && score < bestScore) {
            best = Gdk::MemoryTexture::create(width,
                                              height,
                                              Gdk::MemoryTexture::Format::A8R8G8B8,
                                              Glib::Bytes::create(data, gsize(width) * height * 4),
                                              width * 4);
            bestScore = score;
        }
        g_variant_unref(pixels);
    }
    g_variant_iter_free(pixmaps);
    return best;
}

std::string lookupTooltip(GVariant *properties)
{
    // (icon name, icon pixmaps, title, description)
    const char *title = "";
    const char *description = "";
    g_variant_lookup(properties, "ToolTip", "(&sa(iiay)&s&s)", nullptr, nullptr, &title, &description);
    if (!*title)
        return description;
    if (!*description)
        return title;
    return std::string(title) + "\n" + description;
}
}

TrayItem::TrayItem(Glib::RefPtr<Gio::DBus::Connection> bus, std::string service, std::string path)
    : m_bus(std::move(bus))
    , m_service(std::move(service))
    , m_path(std::move(path))
    // NewIcon, NewStatus, NewToolTip, etc.
    , m_subscription(m_bus->signal_subscribe([this](const auto &...) { refresh(); }, m_service, kItemInterface, {}, m_path))
{
}

TrayItem::~TrayItem()
{
    m_bus->signal_unsubscribe(m_subscription);
}

bool TrayItem::refresh()
{
    Glib::VariantContainerBase reply;
    try {
        reply = m_bus->call_sync(m_path,
                                 "org.freedesktop.DBus.Properties",
                                 "GetAll",
                                 Glib::Variant<std::tuple<Glib::ustring>>::create({kItemInterface}),
                                 m_service,
                                 Config::kDBusTimeout);
    } catch (const Glib::Error &error) {
        g_warning("Could not load tray item %s: %s", id().c_str(), error.what());
        return false;
    }

    GVariant *properties = g_variant_get_child_value(reply.gobj(), 0);

    m_status = lookupString(properties, "Status");

    const auto iconTheme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
    const auto loadIcon = [&](const char *nameKey, const char *pixmapKey) {
        m_iconName = lookupString(properties, nameKey);
        m_iconTexture = iconTheme->has_icon(m_iconName) ? nullptr : lookupPixmap(properties, pixmapKey);
        return iconTheme->has_icon(m_iconName) || m_iconTexture;
    };
    if (!(m_status == "NeedsAttention" && loadIcon("AttentionIconName", "AttentionIconPixmap"))
        && !loadIcon("IconName", "IconPixmap"))
        m_iconName = "application-x-executable";

    m_tooltip = lookupTooltip(properties);
    if (m_tooltip.empty())
        m_tooltip = lookupString(properties, "IconAccessibleDesc");
    if (m_tooltip.empty())
        m_tooltip = lookupString(properties, "Title");

    m_menuPath = lookupString(properties, "Menu", "&o");
    gboolean itemIsMenu = false;
    g_variant_lookup(properties, "ItemIsMenu", "b", &itemIsMenu);
    m_itemIsMenu = itemIsMenu;

    g_variant_unref(properties);
    changed.emit();
    return true;
}

void TrayItem::activate()
{
    call(m_path, kItemInterface, "Activate", g_variant_new("(ii)", 0, 0));
}

void TrayItem::secondaryActivate()
{
    call(m_path, kItemInterface, "SecondaryActivate", g_variant_new("(ii)", 0, 0));
}

void TrayItem::contextMenu()
{
    call(m_path, kItemInterface, "ContextMenu", g_variant_new("(ii)", 0, 0));
}

void TrayItem::scroll(int delta)
{
    call(m_path, kItemInterface, "Scroll", g_variant_new("(is)", delta, "vertical"));
}

Glib::VariantBase TrayItem::menuLayout()
{
    // (parent ID, recursion depth, property names) -> (revision, layout)
    const auto reply = m_bus->call_sync(
        m_menuPath,
        kMenuInterface,
        "GetLayout",
        Glib::Variant<std::tuple<gint32, gint32, std::vector<Glib::ustring>>>::create({0, -1, {}}),
        m_service,
        Config::kDBusTimeout);
    Glib::VariantBase layout;
    reply.get_child(layout, 1);
    return layout;
}

void TrayItem::menuItemClicked(int menuItemId)
{
    call(m_menuPath,
         kMenuInterface,
         "Event",
         g_variant_new("(isvu)", menuItemId, "clicked", g_variant_new_int32(0), guint32(g_get_real_time() / 1000)));
}

void TrayItem::call(const std::string &path, const char *interface, const char *method, GVariant *parameters)
{
    g_dbus_connection_call(m_bus->gobj(),
                           m_service.c_str(),
                           path.c_str(),
                           interface,
                           method,
                           parameters,
                           nullptr,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           nullptr,
                           nullptr,
                           nullptr);
}
