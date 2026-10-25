#include "workspacemanager.h"

#include <glib.h>
#include <wayland-client.h>

#include <algorithm>
#include <cassert>
#include <cstring>

const ext_workspace_handle_v1_listener Workspace::kListener = {
    .id = [](void *, ext_workspace_handle_v1 *, const char *) {},
    .name = [](void *data, ext_workspace_handle_v1 *, const char *name) {
        static_cast<Workspace *>(data)->m_name = name;
    },
    .coordinates = [](void *, ext_workspace_handle_v1 *, wl_array *) {},
    .state = [](void *data, ext_workspace_handle_v1 *, uint32_t state) {
        static_cast<Workspace *>(data)->m_active = state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
    },
    .capabilities = [](void *, ext_workspace_handle_v1 *, uint32_t) {},
    .removed = [](void *data, ext_workspace_handle_v1 *) {
        auto *workspace = static_cast<Workspace *>(data);
        workspace->m_manager.removeWorkspace(workspace);
    },
};

Workspace::Workspace(WorkspaceManager &manager, ext_workspace_handle_v1 *handle)
    : m_manager(manager)
    , m_handle(handle)
{
    ext_workspace_handle_v1_add_listener(m_handle, &kListener, this);
}

Workspace::~Workspace()
{
    ext_workspace_handle_v1_destroy(m_handle);
}

const wl_registry_listener WorkspaceManager::kRegistryListener = {
    .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
        auto *manager = static_cast<WorkspaceManager *>(data);
        if (manager->m_manager || std::strcmp(interface, ext_workspace_manager_v1_interface.name) != 0)
            return;
        manager->m_manager = static_cast<ext_workspace_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, 1));
        ext_workspace_manager_v1_add_listener(manager->m_manager, &kManagerListener, manager);
    },
    .global_remove = [](void *, wl_registry *, uint32_t) {},
};

const ext_workspace_manager_v1_listener WorkspaceManager::kManagerListener = {
    .workspace_group = [](void *, ext_workspace_manager_v1 *, ext_workspace_group_handle_v1 *group) {
        ext_workspace_group_handle_v1_destroy(group);
    },
    .workspace = [](void *data, ext_workspace_manager_v1 *, ext_workspace_handle_v1 *handle) {
        auto *manager = static_cast<WorkspaceManager *>(data);
        manager->m_workspaces.push_back(std::make_unique<Workspace>(*manager, handle));
    },
    .done = [](void *data, ext_workspace_manager_v1 *) {
        static_cast<WorkspaceManager *>(data)->changed.emit();
    },
    .finished = [](void *data, ext_workspace_manager_v1 *) {
        auto *manager = static_cast<WorkspaceManager *>(data);
        ext_workspace_manager_v1_destroy(manager->m_manager);
        manager->m_manager = nullptr;
    },
};

WorkspaceManager::WorkspaceManager(wl_display *display)
    : m_registry(wl_display_get_registry(display))
{
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    wl_display_roundtrip(display);
    if (!m_manager)
        g_warning("The compositor does not support %s; the taskbar shows all workspaces",
                  ext_workspace_manager_v1_interface.name);
}

WorkspaceManager::~WorkspaceManager()
{
    m_workspaces.clear();
    if (m_manager)
        ext_workspace_manager_v1_destroy(m_manager);
    wl_registry_destroy(m_registry);
}

void WorkspaceManager::removeWorkspace(Workspace *workspace)
{
    const auto it = std::ranges::find(m_workspaces, workspace, &std::unique_ptr<Workspace>::get);
    assert(it != m_workspaces.end());
    m_workspaces.erase(it);
}
