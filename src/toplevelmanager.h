#pragma once

#include "wayland-wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

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
    // Fullscreen requests were added in version 2 of the protocol.
    bool supportsFullscreen() const;

    void activate();
    void setMinimized(bool minimized);
    void setMaximized(bool maximized);
    void setFullscreen(bool fullscreen);
    void close();

private:
    static const zwlr_foreign_toplevel_handle_v1_listener kListener;

    ToplevelManager &m_manager;
    zwlr_foreign_toplevel_handle_v1 *m_handle;
    std::string m_title;
    std::string m_appId;
    bool m_activated = false;
    bool m_minimized = false;
    bool m_maximized = false;
    bool m_fullscreen = false;
    std::set<wl_output *> m_outputs;
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
    std::vector<std::unique_ptr<Toplevel>> m_toplevels;
};
