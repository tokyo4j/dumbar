#pragma once

#include "wayland-ext-workspace-v1-client-protocol.h"

#include <sigc++/signal.h>

#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;

class WorkspaceManager;

class Workspace final
{
public:
    Workspace(WorkspaceManager &manager, ext_workspace_handle_v1 *handle);
    ~Workspace();
    Workspace(const Workspace &) = delete;
    Workspace &operator=(const Workspace &) = delete;

    ext_workspace_handle_v1 *handle() const { return m_handle; }
    const std::string &name() const { return m_name; }
    bool active() const { return m_active; }

private:
    static const ext_workspace_handle_v1_listener kListener;

    WorkspaceManager &m_manager;
    ext_workspace_handle_v1 *m_handle;
    std::string m_name;
    bool m_active = false;
};

// Lists the workspaces in the order the compositor announces them. The groups
// are ignored, since jimi has a single one spanning all outputs.
class WorkspaceManager final
{
public:
    explicit WorkspaceManager(wl_display *display);
    ~WorkspaceManager();
    WorkspaceManager(const WorkspaceManager &) = delete;
    WorkspaceManager &operator=(const WorkspaceManager &) = delete;

    const std::vector<std::unique_ptr<Workspace>> &workspaces() const { return m_workspaces; }

    // Emitted whenever the compositor finishes sending a batch of changes.
    sigc::signal<void()> changed;

private:
    friend class Workspace;

    static const wl_registry_listener kRegistryListener;
    static const ext_workspace_manager_v1_listener kManagerListener;

    void removeWorkspace(Workspace *workspace);

    wl_registry *m_registry;
    ext_workspace_manager_v1 *m_manager = nullptr;
    std::vector<std::unique_ptr<Workspace>> m_workspaces;
};
