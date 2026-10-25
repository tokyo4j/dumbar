#include "toplevelmanager.h"

#include "workspacemanager.h"

#include <gdkmm/display.h>
#include <gdkmm/memorytexture.h>
#include <glib.h>
#include <gtkmm/icontheme.h>
#include <sys/mman.h>
#include <unistd.h>
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
        // The pixels are only needed without a themed icon.
        if (toplevel->m_iconChanged && toplevel->m_iconName.empty() && toplevel->m_iconSize > 0) {
            toplevel->m_iconPixels = xfce_foreign_toplevel_handle_v1_get_icon_pixels(
                toplevel->m_xfceHandle, toplevel->m_iconSize, toplevel->m_iconScale);
            xfce_foreign_toplevel_icon_pixels_v1_add_listener(toplevel->m_iconPixels, &kIconPixelsListener, toplevel);
        }
        toplevel->m_iconChanged = false;
        toplevel->m_manager.toplevelUpdated.emit(*toplevel);
    },
    .closed = [](void *data, zwlr_foreign_toplevel_handle_v1 *) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->m_manager.removeToplevel(toplevel);
    },
    .parent = [](void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {},
};

// The events are followed by the done event of the wlr handle.
const xfce_foreign_toplevel_handle_v1_listener Toplevel::kXfceListener = {
    .state = [](void *data, xfce_foreign_toplevel_handle_v1 *, wl_array *states) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->m_sticky = false;
        const auto *state = static_cast<const uint32_t *>(states->data);
        for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i)
            toplevel->m_sticky |= state[i] == XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_STICKY;
    },
    .icon_name = [](void *data, xfce_foreign_toplevel_handle_v1 *, const char *name) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->replaceIcon();
        // The pixels are used instead of a name missing from the theme.
        if (Gtk::IconTheme::get_for_display(Gdk::Display::get_default())->has_icon(name))
            toplevel->m_iconName = name;
    },
    .icon_size = [](void *data, xfce_foreign_toplevel_handle_v1 *, uint32_t size, uint32_t scale) {
        auto *toplevel = static_cast<Toplevel *>(data);
        toplevel->replaceIcon();
        if (size * scale > toplevel->m_iconSize * toplevel->m_iconScale) {
            toplevel->m_iconSize = size;
            toplevel->m_iconScale = scale;
        }
    },
    .no_icon = [](void *data, xfce_foreign_toplevel_handle_v1 *) {
        static_cast<Toplevel *>(data)->replaceIcon();
    },
    .workspace_enter = [](void *data, xfce_foreign_toplevel_handle_v1 *, ext_workspace_handle_v1 *workspace) {
        static_cast<Toplevel *>(data)->m_workspace =
            static_cast<const Workspace *>(ext_workspace_handle_v1_get_user_data(workspace));
    },
    .workspace_leave = [](void *data, xfce_foreign_toplevel_handle_v1 *, ext_workspace_handle_v1 *) {
        static_cast<Toplevel *>(data)->m_workspace = nullptr;
    },
};

const xfce_foreign_toplevel_icon_pixels_v1_listener Toplevel::kIconPixelsListener = {
    .pixels = [](void *data,
                 xfce_foreign_toplevel_icon_pixels_v1 *,
                 int fd,
                 uint32_t width,
                 uint32_t height,
                 uint32_t stride) {
        auto *toplevel = static_cast<Toplevel *>(data);
        const size_t size = size_t(stride) * height;
        void *pixels = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        assert(pixels != MAP_FAILED);
        // Little-endian ARGB32 is BGRA in memory.
        toplevel->m_iconTexture = Gdk::MemoryTexture::create(width,
                                                             height,
                                                             Gdk::MemoryTexture::Format::B8G8R8A8_PREMULTIPLIED,
                                                             Glib::Bytes::create(pixels, size),
                                                             stride);
        munmap(pixels, size);
        toplevel->destroyIconPixels();
        toplevel->m_manager.toplevelUpdated.emit(*toplevel);
    },
    // The app ID decides the icon instead.
    .failed = [](void *data, xfce_foreign_toplevel_icon_pixels_v1 *, uint32_t) {
        static_cast<Toplevel *>(data)->destroyIconPixels();
    },
};

Toplevel::Toplevel(ToplevelManager &manager, zwlr_foreign_toplevel_handle_v1 *handle)
    : m_manager(manager)
    , m_handle(handle)
{
    zwlr_foreign_toplevel_handle_v1_add_listener(m_handle, &kListener, this);
    if (m_manager.m_xfceManager) {
        m_xfceHandle =
            xfce_foreign_toplevel_manager_private_v1_get_xfce_foreign_toplevel_handle(m_manager.m_xfceManager, m_handle);
        xfce_foreign_toplevel_handle_v1_add_listener(m_xfceHandle, &kXfceListener, this);
    }
}

Toplevel::~Toplevel()
{
    destroyIconPixels();
    if (m_xfceHandle)
        xfce_foreign_toplevel_handle_v1_destroy(m_xfceHandle);
    zwlr_foreign_toplevel_handle_v1_destroy(m_handle);
}

// Sticky toplevels are on no workspace either.
bool Toplevel::onActiveWorkspace() const
{
    return !m_workspace || m_workspace->active();
}

// Each group of icon events replaces the previous icon.
void Toplevel::replaceIcon()
{
    if (m_iconChanged)
        return;
    m_iconChanged = true;
    m_iconName.clear();
    m_iconTexture.reset();
    m_iconSize = 0;
    m_iconScale = 0;
    destroyIconPixels();
}

void Toplevel::destroyIconPixels()
{
    if (!m_iconPixels)
        return;
    xfce_foreign_toplevel_icon_pixels_v1_destroy(m_iconPixels);
    m_iconPixels = nullptr;
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

void Toplevel::setSticky(bool sticky)
{
    if (sticky)
        xfce_foreign_toplevel_handle_v1_set_sticky(m_xfceHandle);
    else
        xfce_foreign_toplevel_handle_v1_unset_sticky(m_xfceHandle);
}

void Toplevel::moveToWorkspace(const Workspace &workspace)
{
    xfce_foreign_toplevel_handle_v1_move_to_workspace(m_xfceHandle, workspace.handle());
}

void Toplevel::close()
{
    zwlr_foreign_toplevel_handle_v1_close(m_handle);
}

const wl_registry_listener ToplevelManager::kRegistryListener = {
    .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
        auto *manager = static_cast<ToplevelManager *>(data);
        if (!manager->m_manager && std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
            manager->m_manager = static_cast<zwlr_foreign_toplevel_manager_v1 *>(wl_registry_bind(
                registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min<uint32_t>(version, 3)));
            zwlr_foreign_toplevel_manager_v1_add_listener(manager->m_manager, &kManagerListener, manager);
        } else if (!manager->m_xfceManager
                   && std::strcmp(interface, xfce_foreign_toplevel_manager_private_v1_interface.name) == 0) {
            // Bound along with the wlr manager, before the toplevels are announced.
            manager->m_xfceManager = static_cast<xfce_foreign_toplevel_manager_private_v1 *>(
                wl_registry_bind(registry, name, &xfce_foreign_toplevel_manager_private_v1_interface, 1));
        }
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
    if (m_xfceManager)
        xfce_foreign_toplevel_manager_private_v1_destroy(m_xfceManager);
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
