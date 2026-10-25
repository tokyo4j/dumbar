#pragma once

#include "trayitem.h"

#include <giomm/dbusconnection.h>
#include <sigc++/signal.h>

#include <memory>
#include <vector>

// Collects StatusNotifierItems. Provides the org.kde.StatusNotifierWatcher
// service itself unless another process already does.
class StatusNotifierHost final
{
public:
    StatusNotifierHost();
    ~StatusNotifierHost();
    StatusNotifierHost(const StatusNotifierHost &) = delete;
    StatusNotifierHost &operator=(const StatusNotifierHost &) = delete;

    const std::vector<std::unique_ptr<TrayItem>> &items() const { return m_items; }

    sigc::signal<void(TrayItem &)> itemAdded;
    // Emitted just before the item is destroyed.
    sigc::signal<void(TrayItem &)> itemRemoved;

private:
    void provideWatcher();
    void connectToWatcher();
    // registration is "service", "service/path" or, from the sender itself, "/path".
    void addItem(const std::string &registration, const std::string &sender);
    void removeItem(const std::string &id);
    void emitWatcherSignal(const char *name, const std::string &id);

    Glib::RefPtr<Gio::DBus::Connection> m_bus;
    std::vector<guint> m_subscriptions;
    // Zero unless this process provides the watcher.
    guint m_watcherRegistration = 0;
    std::vector<std::unique_ptr<TrayItem>> m_items;
};
