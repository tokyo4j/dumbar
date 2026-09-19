#include "toplevelmanager.h"

#include "dumbar.h"

#include <QDebug>

#include <algorithm>
#include <cstring>

#include <wayland-client.h>

namespace
{
constexpr auto kManagerInterface = "zwlr_foreign_toplevel_manager_v1";

const zwlr_foreign_toplevel_handle_v1_listener kToplevelListener = {
    Toplevel::titleEvent,
    Toplevel::appIdEvent,
    Toplevel::outputEnterEvent,
    Toplevel::outputLeaveEvent,
    Toplevel::stateEvent,
    Toplevel::doneEvent,
    Toplevel::closedEvent,
    Toplevel::parentEvent,
};

const zwlr_foreign_toplevel_manager_v1_listener kManagerListener = {
    ToplevelManager::managerToplevel,
    ToplevelManager::managerFinished,
};

const wl_registry_listener kRegistryListener = {
    ToplevelManager::registryGlobal,
    ToplevelManager::registryGlobalRemove,
};

const wl_callback_listener kRegistrySyncListener = {
    ToplevelManager::registryScanDone,
};
}

Toplevel::Toplevel(zwlr_foreign_toplevel_handle_v1 *handle, wl_display *display, wl_seat *seat, QObject *parent)
    : QObject(parent)
    , m_handle(handle)
    , m_display(display)
    , m_seat(seat)
{
    zwlr_foreign_toplevel_handle_v1_add_listener(m_handle, &kToplevelListener, this);
    qCDebug(lcDumbar) << "created toplevel"
                              << "handle=" << static_cast<void *>(m_handle)
                              << "seat=" << static_cast<void *>(m_seat);
}

Toplevel::~Toplevel()
{
    qCDebug(lcDumbar) << "destroying toplevel"
                              << "handle=" << static_cast<void *>(m_handle)
                              << "title=" << m_title;
    if (m_handle) {
        zwlr_foreign_toplevel_handle_v1_destroy(m_handle);
        m_handle = nullptr;
        if (m_display)
            wl_display_flush(m_display);
    }
}

void Toplevel::activate(wl_seat *seat)
{
    wl_seat *effectiveSeat = seat ? seat : m_seat;
    if (!m_handle || m_closed || !effectiveSeat)
        return;
    qCDebug(lcDumbar) << "requesting activation"
                              << "title=" << m_title
                              << "seat=" << static_cast<void *>(effectiveSeat);
    zwlr_foreign_toplevel_handle_v1_activate(m_handle, effectiveSeat);
    if (m_display)
        wl_display_flush(m_display);
}

void Toplevel::setMinimized()
{
    if (!m_handle || m_closed)
        return;
    qCDebug(lcDumbar) << "requesting minimize" << "title=" << m_title;
    zwlr_foreign_toplevel_handle_v1_set_minimized(m_handle);
    if (m_display)
        wl_display_flush(m_display);
}

void Toplevel::close()
{
    if (!m_handle || m_closed)
        return;
    qCDebug(lcDumbar) << "requesting close" << "title=" << m_title;
    zwlr_foreign_toplevel_handle_v1_close(m_handle);
    if (m_display)
        wl_display_flush(m_display);
}

void Toplevel::titleEvent(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *title)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    toplevel->m_title = QString::fromUtf8(title ? title : "");
}

void Toplevel::appIdEvent(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *appId)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    toplevel->m_appId = QString::fromUtf8(appId ? appId : "");
}

void Toplevel::outputEnterEvent(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_output *output)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    if (output) {
        toplevel->m_outputs.insert(output);
        qCDebug(lcDumbar) << "toplevel entered output"
                                  << "title=" << toplevel->m_title
                                  << "output=" << static_cast<void *>(output)
                                  << "outputCount=" << toplevel->m_outputs.size();
    }
}

void Toplevel::outputLeaveEvent(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_output *output)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    toplevel->m_outputs.remove(output);
    qCDebug(lcDumbar) << "toplevel left output"
                              << "title=" << toplevel->m_title
                              << "output=" << static_cast<void *>(output)
                              << "outputCount=" << toplevel->m_outputs.size();
}

void Toplevel::stateEvent(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_array *state)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    bool activated = false;
    bool minimized = false;

    const auto *states = static_cast<const uint32_t *>(state->data);
    const size_t count = state->size / sizeof(uint32_t);
    for (size_t i = 0; i < count; ++i) {
        activated |= states[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
        minimized |= states[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
    }
    toplevel->m_activated = activated;
    toplevel->m_minimized = minimized;
}

void Toplevel::doneEvent(void *data, zwlr_foreign_toplevel_handle_v1 *)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    qCDebug(lcDumbar) << "toplevel update"
                              << "title=" << toplevel->m_title
                              << "appId=" << toplevel->m_appId
                              << "activated=" << toplevel->m_activated
                              << "minimized=" << toplevel->m_minimized
                              << "outputCount=" << toplevel->m_outputs.size();
    emit toplevel->updated(toplevel);
}

void Toplevel::closedEvent(void *data, zwlr_foreign_toplevel_handle_v1 *)
{
    auto *toplevel = static_cast<Toplevel *>(data);
    toplevel->m_closed = true;
    qCDebug(lcDumbar) << "toplevel closed"
                              << "title=" << toplevel->m_title
                              << "appId=" << toplevel->m_appId;
    emit toplevel->closed(toplevel);
}

void Toplevel::parentEvent(void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *)
{
}

ToplevelManager::ToplevelManager(wl_display *display, wl_seat *seat, QObject *parent)
    : QObject(parent)
    , m_display(display)
    , m_seat(seat)
{
    qCDebug(lcDumbar) << "creating toplevel manager"
                              << "display=" << static_cast<void *>(m_display)
                              << "seat=" << static_cast<void *>(m_seat);
    if (!m_display) {
        qWarning() << "dumbar: no Wayland display is available";
        return;
    }

    m_registry = wl_display_get_registry(m_display);
    if (!m_registry) {
        qWarning() << "dumbar: could not access the Wayland registry";
        return;
    }

    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    m_registrySync = wl_display_sync(m_display);
    if (m_registrySync)
        wl_callback_add_listener(m_registrySync, &kRegistrySyncListener, this);
    flush();
}

ToplevelManager::~ToplevelManager()
{
    if (m_registrySync) {
        wl_callback_destroy(m_registrySync);
        m_registrySync = nullptr;
    }

    if (m_manager && !m_managerFinished) {
        zwlr_foreign_toplevel_manager_v1_destroy(m_manager);
        m_manager = nullptr;
    }

    if (m_registry) {
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
    }
    flush();
}

void ToplevelManager::registryGlobal(void *data,
                                     wl_registry *registry,
                                     uint32_t name,
                                     const char *interface,
                                     uint32_t version)
{
    auto *manager = static_cast<ToplevelManager *>(data);
    if (manager->m_manager || std::strcmp(interface, kManagerInterface) != 0)
        return;

    const uint32_t bindVersion = std::min<uint32_t>(version, 3);
    qCDebug(lcDumbar) << "found foreign toplevel manager global"
                              << "name=" << name
                              << "version=" << version
                              << "bindVersion=" << bindVersion;
    manager->m_manager = static_cast<zwlr_foreign_toplevel_manager_v1 *>(
        wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, bindVersion));
    if (!manager->m_manager) {
        qWarning() << "dumbar: failed to bind" << kManagerInterface;
        return;
    }

    zwlr_foreign_toplevel_manager_v1_add_listener(manager->m_manager, &kManagerListener, manager);
    manager->m_managerFinished = false;
    qCDebug(lcDumbar) << "foreign toplevel manager bound";
    emit manager->availableChanged(true);
}

void ToplevelManager::registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

void ToplevelManager::registryScanDone(void *data, wl_callback *callback, uint32_t)
{
    auto *manager = static_cast<ToplevelManager *>(data);
    manager->m_registrySync = nullptr;
    wl_callback_destroy(callback);
    qCDebug(lcDumbar) << "Wayland registry scan complete"
                              << "managerAvailable=" << (manager->m_manager != nullptr);
    if (!manager->m_manager && !manager->m_warnedMissing) {
        qWarning() << "dumbar: compositor does not provide" << kManagerInterface << "; taskbar disabled";
        manager->m_warnedMissing = true;
    }
}

void ToplevelManager::managerToplevel(void *data,
                                      zwlr_foreign_toplevel_manager_v1 *,
                                      zwlr_foreign_toplevel_handle_v1 *handle)
{
    auto *manager = static_cast<ToplevelManager *>(data);
    auto *toplevel = new Toplevel(handle, manager->m_display, manager->m_seat, manager);
    manager->m_toplevels.append(toplevel);
    qCDebug(lcDumbar) << "toplevel announced"
                              << "handle=" << static_cast<void *>(handle)
                              << "count=" << manager->m_toplevels.size();
    QObject::connect(toplevel, &Toplevel::closed, manager, [manager](Toplevel *closed) {
        manager->removeToplevel(closed);
    });
    emit manager->toplevelAdded(toplevel);
}

void ToplevelManager::managerFinished(void *data, zwlr_foreign_toplevel_manager_v1 *)
{
    auto *manager = static_cast<ToplevelManager *>(data);
    manager->m_manager = nullptr;
    manager->m_managerFinished = true;
    qCDebug(lcDumbar) << "foreign toplevel manager finished";
    emit manager->availableChanged(false);
}

void ToplevelManager::removeToplevel(Toplevel *toplevel)
{
    if (!m_toplevels.removeOne(toplevel))
        return;
    qCDebug(lcDumbar) << "removing toplevel"
                              << "title=" << toplevel->title()
                              << "remaining=" << m_toplevels.size();
    emit toplevelRemoved(toplevel);
    toplevel->deleteLater();
}

void ToplevelManager::flush()
{
    if (m_display)
        wl_display_flush(m_display);
}
