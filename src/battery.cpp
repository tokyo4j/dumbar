#include "battery.h"

#include "config.h"

#include <cmath>
#include <format>

namespace
{
// org.freedesktop.UPower.Device.State
enum State : guint32 {
    Charging = 1,
    Discharging = 2,
    Empty = 3,
    FullyCharged = 4,
    PendingCharge = 5,
    PendingDischarge = 6,
};

const char *stateName(guint32 state)
{
    switch (state) {
    case Charging:
        return "Charging";
    case Discharging:
        return "Discharging";
    case Empty:
        return "Empty";
    case FullyCharged:
        return "Fully charged";
    case PendingCharge:
        return "Pending charge";
    case PendingDischarge:
        return "Pending discharge";
    default:
        return "Unknown state";
    }
}

std::string formatDuration(gint64 seconds)
{
    const gint64 hours = seconds / 3600;
    const gint64 minutes = seconds % 3600 / 60;
    if (hours > 0)
        return std::format("{}h {}m", hours, minutes);
    return std::format("{}m", std::max<gint64>(1, minutes));
}

template<typename T>
T property(const Glib::RefPtr<Gio::DBus::Proxy> &proxy, const Glib::ustring &name)
{
    Glib::Variant<T> value;
    proxy->get_cached_property(value, name);
    return value.get();
}
}

Battery::Battery()
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 2)
{
    m_icon.set_pixel_size(Config::kIconSize);
    append(m_icon);
    append(m_label);
    set_visible(false);

    try {
        m_proxy = Gio::DBus::Proxy::create_for_bus_sync(Gio::DBus::BusType::SYSTEM,
                                                        "org.freedesktop.UPower",
                                                        "/org/freedesktop/UPower/devices/DisplayDevice",
                                                        "org.freedesktop.UPower.Device");
    } catch (const Glib::Error &error) {
        g_warning("Battery is disabled: %s", error.what());
        return;
    }
    m_proxy->signal_properties_changed().connect([this](const auto &, const auto &) { refresh(); });
    refresh();
}

void Battery::refresh()
{
    // Cached properties are only available while UPower is running.
    set_visible(!m_proxy->get_name_owner().empty() && property<bool>(m_proxy, "IsPresent"));
    if (!get_visible())
        return;

    const auto percentage = std::lround(property<double>(m_proxy, "Percentage"));
    const auto state = property<guint32>(m_proxy, "State");
    m_icon.set_from_icon_name(property<Glib::ustring>(m_proxy, "IconName"));
    m_label.set_text(std::format("{}%", percentage));

    std::string tooltip = std::format("Battery: {}%\n{}", percentage, stateName(state));
    gint64 remaining = 0;
    if (state == Discharging)
        remaining = property<gint64>(m_proxy, "TimeToEmpty");
    else if (state == Charging || state == PendingCharge)
        remaining = property<gint64>(m_proxy, "TimeToFull");
    if (remaining > 0)
        tooltip += std::format("\n{} remaining", formatDuration(remaining));
    set_tooltip_text(tooltip);
}
