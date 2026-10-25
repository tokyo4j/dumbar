#pragma once

#include "wayland-wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wayland-xfce-foreign-toplevel-management-private-v1-client-protocol.h"

#include <gdkmm/texture.h>
#include <sigc++/signal.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

struct wl_display;
struct wl_output;
struct wl_registry;
struct wl_seat;

class ToplevelManager;
class Workspace;

class Toplevel final
{
public:
    Toplevel(ToplevelManager &manager, zwlr_foreign_toplevel_handle_v1 *handle);
    ~Toplevel();
    Toplevel(const Toplevel &) = delete;
    Toplevel &operator=(const Toplevel &) = delete;

    const std::string &title() const { return m_title; }
    const std::string &appId() const { return m_appId; }
    bool activated() const { return m_activated; }
    bool minimized() const { return m_minimized; }
    bool maximized() const { return m_maximized; }
    bool fullscreen() const { return m_fullscreen; }
    bool onOutput(wl_output *output) const { return m_outputs.contains(output); }
    bool sticky() const { return m_sticky; }
    // Null if the toplevel is sticky, or its workspace is unknown.
    const Workspace *workspace() const { return m_workspace; }
    bool onActiveWorkspace() const;
    // A themed icon, an icon texture, or neither, in which case the app ID
    // decides the icon.
    const std::string &iconName() const { return m_iconName; }
    const Glib::RefPtr<Gdk::Texture> &iconTexture() const { return m_iconTexture; }
    // Fullscreen requests were added in version 2 of the protocol.
    bool supportsFullscreen() const;
    // Sticky and workspace requests need the xfce extensions.
    bool supportsXfce() const { return m_xfceHandle; }

    void activate();
    void setMinimized(bool minimized);
    void setMaximized(bool maximized);
    void setFullscreen(bool fullscreen);
    void setSticky(bool sticky);
    void moveToWorkspace(const Workspace &workspace);
    void close();

private:
    static const zwlr_foreign_toplevel_handle_v1_listener kListener;
    static const xfce_foreign_toplevel_handle_v1_listener kXfceListener;
    static const xfce_foreign_toplevel_icon_pixels_v1_listener kIconPixelsListener;

    void replaceIcon();
    void destroyIconPixels();

    ToplevelManager &m_manager;
    zwlr_foreign_toplevel_handle_v1 *m_handle;
    xfce_foreign_toplevel_handle_v1 *m_xfceHandle = nullptr;
    std::string m_title;
    std::string m_appId;
    bool m_activated = false;
    bool m_minimized = false;
    bool m_maximized = false;
    bool m_fullscreen = false;
    std::set<wl_output *> m_outputs;
    bool m_sticky = false;
    const Workspace *m_workspace = nullptr;
    std::string m_iconName;
    Glib::RefPtr<Gdk::Texture> m_iconTexture;
    // Set by the icon events until the done event.
    bool m_iconChanged = false;
    // The largest icon size advertised, to request the pixels of.
    uint32_t m_iconSize = 0;
    uint32_t m_iconScale = 0;
    xfce_foreign_toplevel_icon_pixels_v1 *m_iconPixels = nullptr;
};

class ToplevelManager final
{
public:
    ToplevelManager(wl_display *display, wl_seat *seat);
    ~ToplevelManager();
    ToplevelManager(const ToplevelManager &) = delete;
    ToplevelManager &operator=(const ToplevelManager &) = delete;

    const std::vector<std::unique_ptr<Toplevel>> &toplevels() const { return m_toplevels; }

    // Emitted whenever the compositor finishes sending a batch of changes,
    // including right after a toplevel is announced.
    sigc::signal<void(Toplevel &)> toplevelUpdated;
    // Emitted just before the toplevel is destroyed.
    sigc::signal<void(Toplevel &)> toplevelRemoved;

private:
    friend class Toplevel;

    static const wl_registry_listener kRegistryListener;
    static const zwlr_foreign_toplevel_manager_v1_listener kManagerListener;

    void removeToplevel(Toplevel *toplevel);

    wl_seat *m_seat;
    wl_registry *m_registry;
    zwlr_foreign_toplevel_manager_v1 *m_manager = nullptr;
    xfce_foreign_toplevel_manager_private_v1 *m_xfceManager = nullptr;
    std::vector<std::unique_ptr<Toplevel>> m_toplevels;
};
