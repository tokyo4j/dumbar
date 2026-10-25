#include "statusnotifierhost.h"

#include <giomm/dbusintrospection.h>

#include <algorithm>

namespace
{
constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
constexpr auto kWatcherInterface = "org.kde.StatusNotifierWatcher";
constexpr auto kDefaultItemPath = "/StatusNotifierItem";

constexpr auto kWatcherXml = R"xml(
<node>
  <interface name="org.kde.StatusNotifierWatcher">
    <method name="RegisterStatusNotifierItem">
      <arg name="service" type="s" direction="in"/>
    </method>
    <method name="RegisterStatusNotifierHost">
      <arg name="service" type="s" direction="in"/>
    </method>
    <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
    <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
    <property name="ProtocolVersion" type="i" access="read"/>
    <signal name="StatusNotifierItemRegistered">
      <arg type="s"/>
    </signal>
    <signal name="StatusNotifierItemUnregistered">
      <arg type="s"/>
    </signal>
    <signal name="StatusNotifierHostRegistered"/>
  </interface>
</node>
)xml";

// DBUS_NAME_FLAG_DO_NOT_QUEUE and DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
constexpr guint32 kNameFlagDoNotQueue = 4;
constexpr guint32 kNameReplyPrimaryOwner = 1;
}

StatusNotifierHost::StatusNotifierHost()
    : m_bus(Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION))
{
    // Items vanish without unregistering when their process exits.
    m_subscriptions.push_back(m_bus->signal_subscribe(
        [this](const auto &, const auto &, const auto &, const auto &, const auto &,
               Glib::VariantContainerBase parameters) {
            const char *name, *oldOwner, *newOwner;
            g_variant_get(parameters.gobj(), "(&s&s&s)", &name, &oldOwner, &newOwner);
            if (*newOwner)
                return;
            std::vector<std::string> ids;
            for (const auto &item : m_items) {
                if (item->service() == name)
                    ids.push_back(item->id());
            }
            for (const std::string &id : ids)
                removeItem(id);
        },
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "NameOwnerChanged",
        "/org/freedesktop/DBus"));

    auto reply = m_bus->call_sync("/org/freedesktop/DBus",
                                  "org.freedesktop.DBus",
                                  "RequestName",
                                  Glib::Variant<std::tuple<Glib::ustring, guint32>>::create(
                                      {kWatcherService, kNameFlagDoNotQueue}),
                                  "org.freedesktop.DBus");
    guint32 result;
    g_variant_get(reply.gobj(), "(u)", &result);
    if (result == kNameReplyPrimaryOwner)
        provideWatcher();
    else
        connectToWatcher();
}

StatusNotifierHost::~StatusNotifierHost()
{
    for (const guint subscription : m_subscriptions)
        m_bus->signal_unsubscribe(subscription);
    if (m_watcherRegistration)
        m_bus->unregister_object(m_watcherRegistration);
}

void StatusNotifierHost::provideWatcher()
{
    m_watcherRegistration = m_bus->register_object(
        kWatcherPath,
        Gio::DBus::NodeInfo::create_for_xml(kWatcherXml)->lookup_interface(),
        [this](const auto &, const Glib::ustring &sender, const auto &, const auto &, const Glib::ustring &method,
               Glib::VariantContainerBase parameters,
               const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation) {
            // Reply first: the item may not answer our requests while it waits for the reply.
            invocation->return_value({});
            if (method == "RegisterStatusNotifierItem") {
                const char *registration;
                g_variant_get(parameters.gobj(), "(&s)", &registration);
                addItem(registration, sender);
            }
        },
        [this](Glib::VariantBase &value, const auto &, const auto &, const auto &, const auto &,
               const Glib::ustring &property) {
            if (property == "RegisteredStatusNotifierItems") {
                std::vector<Glib::ustring> ids;
                for (const auto &item : m_items)
                    ids.push_back(item->id());
                value = Glib::Variant<std::vector<Glib::ustring>>::create(ids);
            } else if (property == "IsStatusNotifierHostRegistered") {
                value = Glib::Variant<bool>::create(true);
            } else if (property == "ProtocolVersion") {
                value = Glib::Variant<gint32>::create(0);
            }
        });
    m_bus->emit_signal(kWatcherPath, kWatcherInterface, "StatusNotifierHostRegistered");
}

void StatusNotifierHost::connectToWatcher()
{
    m_subscriptions.push_back(m_bus->signal_subscribe(
        [this](const auto &, const auto &, const auto &, const auto &, const auto &,
               Glib::VariantContainerBase parameters) {
            const char *id;
            g_variant_get(parameters.gobj(), "(&s)", &id);
            addItem(id, {});
        },
        kWatcherService,
        kWatcherInterface,
        "StatusNotifierItemRegistered",
        kWatcherPath));
    m_subscriptions.push_back(m_bus->signal_subscribe(
        [this](const auto &, const auto &, const auto &, const auto &, const auto &,
               Glib::VariantContainerBase parameters) {
            const char *id;
            g_variant_get(parameters.gobj(), "(&s)", &id);
            removeItem(id);
        },
        kWatcherService,
        kWatcherInterface,
        "StatusNotifierItemUnregistered",
        kWatcherPath));

    m_bus->call_sync(kWatcherPath,
                     kWatcherInterface,
                     "RegisterStatusNotifierHost",
                     Glib::Variant<std::tuple<Glib::ustring>>::create({m_bus->get_unique_name()}),
                     kWatcherService);

    auto reply = m_bus->call_sync(kWatcherPath,
                                  "org.freedesktop.DBus.Properties",
                                  "Get",
                                  Glib::Variant<std::tuple<Glib::ustring, Glib::ustring>>::create(
                                      {kWatcherInterface, "RegisteredStatusNotifierItems"}),
                                  kWatcherService);
    GVariant *ids;
    g_variant_get(reply.gobj(), "(v)", &ids);
    GVariantIter iter;
    g_variant_iter_init(&iter, ids);
    const char *id;
    while (g_variant_iter_next(&iter, "&s", &id))
        addItem(id, {});
    g_variant_unref(ids);
}

void StatusNotifierHost::addItem(const std::string &registration, const std::string &sender)
{
    std::string id = registration;
    if (registration.starts_with('/'))
        id = sender + registration;
    else if (registration.find('/') == std::string::npos)
        id = registration + kDefaultItemPath;

    if (std::ranges::any_of(m_items, [&](const auto &item) { return item->id() == id; }))
        return;

    const size_t slash = id.find('/');
    auto item = std::make_unique<TrayItem>(m_bus, id.substr(0, slash), id.substr(slash));
    if (!item->refresh())
        return;
    m_items.push_back(std::move(item));
    itemAdded.emit(*m_items.back());
    emitWatcherSignal("StatusNotifierItemRegistered", id);
}

void StatusNotifierHost::removeItem(const std::string &id)
{
    const auto it = std::ranges::find_if(m_items, [&](const auto &item) { return item->id() == id; });
    if (it == m_items.end())
        return;
    itemRemoved.emit(**it);
    m_items.erase(it);
    emitWatcherSignal("StatusNotifierItemUnregistered", id);
}

void StatusNotifierHost::emitWatcherSignal(const char *name, const std::string &id)
{
    if (m_watcherRegistration)
        m_bus->emit_signal(kWatcherPath, kWatcherInterface, name, {}, Glib::Variant<std::tuple<Glib::ustring>>::create({id}));
}
