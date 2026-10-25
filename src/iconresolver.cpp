#include "iconresolver.h"

#include <giomm/desktopappinfo.h>
#include <giomm/themedicon.h>

namespace
{
constexpr auto kFallbackIcon = "application-x-executable";

std::string lowercase(const std::string &text)
{
    return Glib::ustring(text).lowercase();
}
}

IconResolver::IconResolver()
{
    for (const auto &appInfo : Gio::AppInfo::get_all()) {
        const auto desktopAppInfo = std::dynamic_pointer_cast<Gio::DesktopAppInfo>(appInfo);
        const auto icon = appInfo->get_icon();
        if (!desktopAppInfo || !icon)
            continue;

        std::string id = lowercase(appInfo->get_id());
        if (id.ends_with(".desktop"))
            id.resize(id.size() - std::string_view(".desktop").size());
        m_icons.try_emplace(id, icon);

        const std::string wmClass = desktopAppInfo->get_startup_wm_class();
        if (!wmClass.empty())
            m_icons.try_emplace(lowercase(wmClass), icon);
    }
}

Glib::RefPtr<Gio::Icon> IconResolver::iconForAppId(const std::string &appId) const
{
    const auto it = m_icons.find(lowercase(appId));
    if (it == m_icons.end())
        return Gio::ThemedIcon::create(kFallbackIcon);
    return it->second;
}
