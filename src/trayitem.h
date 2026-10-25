#pragma once

#include <gdkmm/texture.h>
#include <giomm/dbusconnection.h>
#include <sigc++/signal.h>

#include <string>

// A StatusNotifierItem exported by an application.
class TrayItem final
{
public:
    TrayItem(Glib::RefPtr<Gio::DBus::Connection> bus, std::string service, std::string path);
    ~TrayItem();
    TrayItem(const TrayItem &) = delete;
    TrayItem &operator=(const TrayItem &) = delete;

    std::string id() const { return m_service + m_path; }
    const std::string &service() const { return m_service; }
    bool passive() const { return m_status == "Passive"; }
    bool itemIsMenu() const { return m_itemIsMenu; }
    bool hasMenu() const { return !m_menuPath.empty(); }
    const std::string &tooltip() const { return m_tooltip; }
    // The icon is either a texture or, if the texture is null, a themed icon name.
    const Glib::RefPtr<Gdk::Texture> &iconTexture() const { return m_iconTexture; }
    const std::string &iconName() const { return m_iconName; }

    // Reloads all properties. Returns false if the item does not respond.
    bool refresh();

    void activate();
    void secondaryActivate();
    void contextMenu();
    void scroll(int delta);

    // Returns the current com.canonical.dbusmenu layout, (ia{sv}av). Throws Glib::Error.
    Glib::VariantBase menuLayout();
    void menuItemClicked(int menuItemId);

    sigc::signal<void()> changed;

private:
    void call(const std::string &path, const char *interface, const char *method, GVariant *parameters);

    Glib::RefPtr<Gio::DBus::Connection> m_bus;
    std::string m_service;
    std::string m_path;
    guint m_subscription;

    std::string m_status;
    std::string m_iconName;
    Glib::RefPtr<Gdk::Texture> m_iconTexture;
    std::string m_tooltip;
    std::string m_menuPath;
    bool m_itemIsMenu = false;
};
