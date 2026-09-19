#pragma once

#include "wayland-wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

struct wl_display;
struct wl_callback;
struct wl_registry;
struct wl_seat;

class Toplevel final : public QObject
{
    Q_OBJECT

public:
    Toplevel(zwlr_foreign_toplevel_handle_v1 *handle,
             wl_display *display,
             wl_seat *seat,
             QObject *parent = nullptr);
    ~Toplevel() override;

    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    bool activated() const { return m_activated; }
    bool minimized() const { return m_minimized; }
    bool enteredOutput(wl_output *output) const { return output && m_outputs.contains(output); }

    void activate(struct wl_seat *seat = nullptr);
    void setMinimized();
    void close();

signals:
    void updated(Toplevel *toplevel);
    void closed(Toplevel *toplevel);

public:
    static void titleEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle, const char *title);
    static void appIdEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle, const char *appId);
    static void outputEnterEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output);
    static void outputLeaveEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output);
    static void stateEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state);
    static void doneEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle);
    static void closedEvent(void *data, zwlr_foreign_toplevel_handle_v1 *handle);
    static void parentEvent(void *data,
                            zwlr_foreign_toplevel_handle_v1 *handle,
                            zwlr_foreign_toplevel_handle_v1 *parent);

private:
    zwlr_foreign_toplevel_handle_v1 *m_handle = nullptr;
    wl_display *m_display = nullptr;
    wl_seat *m_seat = nullptr;
    QString m_title;
    QString m_appId;
    bool m_activated = false;
    bool m_minimized = false;
    bool m_closed = false;
    QSet<wl_output *> m_outputs;
};

class ToplevelManager final : public QObject
{
    Q_OBJECT

public:
    ToplevelManager(wl_display *display, struct wl_seat *seat, QObject *parent = nullptr);
    ~ToplevelManager() override;

    bool isAvailable() const { return m_manager != nullptr && !m_managerFinished; }
    struct wl_seat *seat() const { return m_seat; }
    const QList<Toplevel *> &toplevels() const { return m_toplevels; }

signals:
    void availableChanged(bool available);
    void toplevelAdded(Toplevel *toplevel);
    void toplevelRemoved(Toplevel *toplevel);

public:
    static void registryGlobal(void *data,
                               wl_registry *registry,
                               uint32_t name,
                               const char *interface,
                               uint32_t version);
    static void registryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void registryScanDone(void *data, struct wl_callback *callback, uint32_t serial);

    static void managerToplevel(void *data,
                                zwlr_foreign_toplevel_manager_v1 *manager,
                                zwlr_foreign_toplevel_handle_v1 *handle);
    static void managerFinished(void *data, zwlr_foreign_toplevel_manager_v1 *manager);

private:
    void removeToplevel(Toplevel *toplevel);
    void flush();

    wl_display *m_display = nullptr;
    struct wl_seat *m_seat = nullptr;
    wl_registry *m_registry = nullptr;
    wl_callback *m_registrySync = nullptr;
    zwlr_foreign_toplevel_manager_v1 *m_manager = nullptr;
    QList<Toplevel *> m_toplevels;
    bool m_managerFinished = false;
    bool m_warnedMissing = false;
};
