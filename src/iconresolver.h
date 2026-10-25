#pragma once

#include <giomm/icon.h>

#include <string>
#include <unordered_map>

// Maps Wayland app IDs to the icons of their desktop entries.
class IconResolver final
{
public:
    IconResolver();

    Glib::RefPtr<Gio::Icon> iconForAppId(const std::string &appId) const;

private:
    // Keyed by lower-cased desktop file ID (without ".desktop") and StartupWMClass.
    std::unordered_map<std::string, Glib::RefPtr<Gio::Icon>> m_icons;
};
