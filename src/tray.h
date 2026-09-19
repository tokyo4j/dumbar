#pragma once

#include "trayitem.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QHash>
#include <QPointer>
#include <QWidget>

class QDBusServiceWatcher;
class LayerShellMenu;

class Tray final : public QWidget, protected QDBusContext
{
    Q_OBJECT

public:
    explicit Tray(QWidget *parent = nullptr);
    ~Tray() override;

public slots:
    QString registerItem(const QString &registration);
    void unregisterItem(const QString &itemId);

public:
    void removeItemsForService(const QString &service);
    QStringList registeredItems() const;

private:
    void serviceUnregistered(const QString &service);
    void connectToWatcher();
    void setupLocalWatcher();
    void loadRegisteredItems();
    void removeItem(const QString &itemId);
    void closePopup();
    void showPopup(TrayItem *item);
    void updateVisibility();

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    QObject *m_localWatcher = nullptr;
    class QHBoxLayout *m_layout = nullptr;
    QHash<QString, TrayItem *> m_items;
    QPointer<LayerShellMenu> m_popup;
    QPointer<TrayItem> m_popupItem;
    bool m_ownsWatcher = false;
};
