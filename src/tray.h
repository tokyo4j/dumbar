#pragma once

#include "trayitem.h"

#include <QDBusConnection>
#include <QHash>
#include <QWidget>

class QDBusServiceWatcher;

class Tray final : public QWidget
{
    Q_OBJECT

public:
    explicit Tray(QWidget *parent = nullptr);
    ~Tray() override;

public slots:
    void registerItem(const QString &itemId);
    void unregisterItem(const QString &itemId);

public:
    void removeItemsForService(const QString &service);
    QStringList registeredItems() const;

private slots:
    void itemChanged(TrayItem *item);
    void itemInvalid(TrayItem *item);
    void serviceUnregistered(const QString &service);

private:
    void connectToWatcher();
    void setupLocalWatcher();
    void loadRegisteredItems();
    void removeItem(const QString &itemId);
    void updateVisibility();

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    QObject *m_localWatcher = nullptr;
    class QHBoxLayout *m_layout = nullptr;
    QHash<QString, TrayItem *> m_items;
    QHash<QString, QWidget *> m_buttons;
    bool m_ownsWatcher = false;
    bool m_warned = false;
};
