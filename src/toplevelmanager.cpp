#include "toplevelmanager.h"

#include <glib.h>
#include <wayland-client.h>

#include <algorithm>
#include <cassert>
#include <cstring>

const zwlr_foreign_toplevel_handle_v1_listener Toplevel::kListener = {
    .title = [](void *data, zwlr_foreign_toplevel_handle_v1 *, const char *title) {
        static_cast<Toplevel *>(data)->m_title = title;
    },
    .app_id = [](void *data, zwlr_foreign_toplevel_handle_v1 *, const char *appId) {
        static_cast<Toplevel *>(data)->m_appId = appId;
    },
    .output_enter = [](void *data, zwlr_foreign_toplevel_handle_v1 *, wl_output *output) {
        static_cast<Toplevel *>(data)->m_outputs.insert(output);
    },
    .output_leave = [](void *data, zwlr_foreign_toplevel_handle_v1 *, wl_output *output) {
        static_cast<Toplevel *>(data)->m_outputs.erase(output);
    },
    .state = [](void *data, zwlr_foreign_toplevel_handle_v1 *, wl_array *states) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->m_activated = false;
        toplevel->m_minimized = false;
        toplevel->m_maximized = false;
        toplevel->m_fullscreen = false;
        const auto *state = static_cast<const uint32_t *>(states->data);
        for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i) {
            toplevel->m_activated |= state[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
            toplevel->m_minimized |= state[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
            toplevel->m_maximized |= state[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
            toplevel->m_fullscreen |= state[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
        }
    },
    .done = [](void *data, zwlr_foreign_toplevel_handle_v1 *) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->m_manager.toplevelUpdated.emit(*toplevel);
    },
    .closed = [](void *data, zwlr_foreign_toplevel_handle_v1 *) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->m_manager.removeToplevel(toplevel);
    },
    .parent = [](void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {},
};

Toplevel::Toplevel(ToplevelManager &manager, zwlr_foreign_toplevel_handle_v1 *handle)
    : m_manager(manager)
    , m_handle(handle)
{
    zwlr_foreign_toplevel_handle_v1_add_listener(m_handle, &kListener, this);
}

Toplevel::~Toplevel()
{
    zwlr_foreign_toplevel_handle_v1_destroy(m_handle);
}

void Toplevel::activate()
{
    zwlr_foreign_toplevel_handle_v1_activate(m_handle, m_manager.m_seat);
}

bool Toplevel::supportsFullscreen() const
{
    return zwlr_foreign_toplevel_handle_v1_get_version(m_handle)
           >= ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_SET_FULLSCREEN_SINCE_VERSION;
}

void Toplevel::setMinimized(bool minimized)
{
    if (minimized)
        zwlr_foreign_toplevel_handle_v1_set_minimized(m_handle);
    else
        zwlr_foreign_toplevel_handle_v1_unset_minimized(m_handle);
}

void Toplevel::setMaximized(bool maximized)
{
    if (maximized)
        zwlr_foreign_toplevel_handle_v1_set_maximized(m_handle);
    else
        zwlr_foreign_toplevel_handle_v1_unset_maximized(m_handle);
}

void Toplevel::setFullscreen(bool fullscreen)
{
    // A null output lets the compositor choose.
    if (fullscreen)
        zwlr_foreign_toplevel_handle_v1_set_fullscreen(m_handle, nullptr);
    else
        zwlr_foreign_toplevel_handle_v1_unset_fullscreen(m_handle);
}

void Toplevel::close()
{
    zwlr_foreign_toplevel_handle_v1_close(m_handle);
}

const wl_registry_listener ToplevelManager::kRegistryListener = {
    .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
        auto *manager = static_cast<ToplevelManager *>(data);
        if (manager->m_manager || std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) != 0)
            return;
        manager->m_manager = static_cast<zwlr_foreign_toplevel_manager_v1 *>(wl_registry_bind(
            registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min<uint32_t>(version, 3)));
        zwlr_foreign_toplevel_manager_v1_add_listener(manager->m_manager, &kManagerListener, manager);
    },
    .global_remove = [](void *, wl_registry *, uint32_t) {},
};

const zwlr_foreign_toplevel_manager_v1_listener ToplevelManager::kManagerListener = {
    .toplevel = [](void *data, zwlr_foreign_toplevel_manager_v1 *, zwlr_foreign_toplevel_handle_v1 *handle) {
        auto *manager = static_cast<ToplevelManager *>(data);
        manager->m_toplevels.push_back(std::make_unique<Toplevel>(*manager, handle));
    },
    .finished = [](void *data, zwlr_foreign_toplevel_manager_v1 *) {
        auto *manager = static_cast<ToplevelManager *>(data);
        zwlr_foreign_toplevel_manager_v1_destroy(manager->m_manager);
        manager->m_manager = nullptr;
    },
};

ToplevelManager::ToplevelManager(wl_display *display, wl_seat *seat)
    : m_seat(seat)
    , m_registry(wl_display_get_registry(display))
{
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    wl_display_roundtrip(display);
    if (!m_manager)
        g_warning("The compositor does not support %s; the taskbar is disabled",
                  zwlr_foreign_toplevel_manager_v1_interface.name);
}

ToplevelManager::~ToplevelManager()
{
    m_toplevels.clear();
    if (m_manager)
        zwlr_foreign_toplevel_manager_v1_destroy(m_manager);
    wl_registry_destroy(m_registry);
}

void ToplevelManager::removeToplevel(Toplevel *toplevel)
{
    const auto it = std::ranges::find(m_toplevels, toplevel, &std::unique_ptr<Toplevel>::get);
    assert(it != m_toplevels.end());
    toplevelRemoved.emit(*toplevel);
    m_toplevels.erase(it);
}
