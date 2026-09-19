#include "tray.h"

#include "dumbar.h"
#include "trayitem.h"

#include <QDBusArgument>
#include <QDBusAbstractAdaptor>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>
#include <QWheelEvent>

namespace
{
constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
constexpr auto kWatcherInterface = "org.kde.StatusNotifierWatcher";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

QVariant unbox(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariant>(value.value<QDBusArgument>());
    return value;
}

QStringList stringListFromValue(const QVariant &value)
{
    const QVariant unboxed = unbox(value);
    if (unboxed.metaType() == QMetaType::fromType<QStringList>())
        return unboxed.toStringList();
    if (unboxed.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QStringList>(unboxed.value<QDBusArgument>());
    return {};
}

class StatusNotifierWatcherAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered)
    Q_PROPERTY(uint ProtocolVersion READ protocolVersion)

public:
    explicit StatusNotifierWatcherAdaptor(Tray *tray)
        : QDBusAbstractAdaptor(tray)
        , m_tray(tray)
    {
    }

    QStringList registeredItems() const { return m_items; }
    bool isHostRegistered() const { return m_hostRegistered; }
    uint protocolVersion() const { return 0; }

    void removeService(const QString &service)
    {
        qCDebug(lcDumbar) << "status notifier service unregistered" << "service=" << service;
        const QStringList items = m_items;
        for (const QString &item : items) {
            const QString itemService = item.section(QLatin1Char('/'), 0, 0);
            if (itemService != service)
                continue;
            m_items.removeAll(item);
            emit StatusNotifierItemUnregistered(item);
        }
    }

public slots:
    void RegisterStatusNotifierItem(const QString &service)
    {
        if (service.isEmpty())
            return;
        qCDebug(lcDumbar) << "status notifier item registered" << "item=" << service;
        if (!m_items.contains(service)) {
            m_items.append(service);
            emit StatusNotifierItemRegistered(service);
        }
        m_tray->registerItem(service);
    }

    void RegisterStatusNotifierHost(const QString &service)
    {
        Q_UNUSED(service)
        m_hostRegistered = true;
        emit IsStatusNotifierHostRegisteredChanged();
    }

signals:
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void IsStatusNotifierHostRegisteredChanged();

private:
    Tray *m_tray = nullptr;
    QStringList m_items;
    bool m_hostRegistered = false;
};

class TrayButton final : public QToolButton
{
public:
    TrayButton(TrayItem *item, QWidget *parent)
        : QToolButton(parent)
        , m_item(item)
    {
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setIconSize(QSize(18, 18));
        setFixedSize(22, 24);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        refresh();
    }

    void refresh()
    {
        setIcon(m_item->icon());
        setToolTip(m_item->toolTip());
        setVisible(!m_item->isPassive());
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QPoint point = event->position().toPoint();
        if (event->button() == Qt::LeftButton)
            m_item->activate(point.x(), point.y());
        else if (event->button() == Qt::MiddleButton)
            m_item->secondaryActivate(point.x(), point.y());
        else if (event->button() == Qt::RightButton)
            m_item->contextMenu(point.x(), point.y());
        event->accept();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            m_item->scroll(delta, QStringLiteral("vertical"));
        event->accept();
    }

private:
    TrayItem *m_item = nullptr;
};
}

Tray::Tray(QWidget *parent)
    : QWidget(parent)
    , m_bus(QDBusConnection::sessionBus())
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(1);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    hide();

    qCDebug(lcDumbar) << "creating system tray" << "dbusConnected=" << m_bus.isConnected();

    if (!m_bus.isConnected()) {
        qWarning("dumbar: session D-Bus is unavailable; system tray disabled");
        m_warned = true;
        return;
    }

    m_serviceWatcher = new QDBusServiceWatcher(this);
    m_serviceWatcher->setConnection(m_bus);
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &Tray::serviceUnregistered);

    const bool watcherExists = m_bus.interface()
        && m_bus.interface()->isServiceRegistered(QString::fromLatin1(kWatcherService)).value();
    if (watcherExists) {
        connectToWatcher();
    } else if (m_bus.registerService(QString::fromLatin1(kWatcherService))) {
        m_ownsWatcher = true;
        setupLocalWatcher();
    } else if (m_bus.interface()
               && m_bus.interface()->isServiceRegistered(QString::fromLatin1(kWatcherService)).value()) {
        connectToWatcher();
    } else {
        qWarning("dumbar: could not provide or find a StatusNotifierWatcher; system tray disabled");
        m_warned = true;
    }
}

Tray::~Tray()
{
    qCDebug(lcDumbar) << "destroying system tray" << "itemCount=" << m_items.size();
    if (m_ownsWatcher) {
        m_bus.unregisterObject(QString::fromLatin1(kWatcherPath));
        m_bus.unregisterService(QString::fromLatin1(kWatcherService));
    }
}

void Tray::registerItem(const QString &itemId)
{
    if (itemId.isEmpty())
        return;
    if (m_items.contains(itemId)) {
        qCDebug(lcDumbar) << "ignoring duplicate tray item" << "item=" << itemId;
        return;
    }

    const int slash = itemId.indexOf(QLatin1Char('/'));
    const QString service = slash < 0 ? itemId : itemId.left(slash);
    const QString path = slash < 0 ? QStringLiteral("/StatusNotifierItem") : itemId.mid(slash);
    if (service.isEmpty() || !path.startsWith(QLatin1Char('/'))) {
        qCDebug(lcDumbar) << "ignoring malformed tray item" << "item=" << itemId;
        return;
    }

    qCDebug(lcDumbar) << "creating tray item"
                      << "item=" << itemId
                      << "service=" << service
                      << "path=" << path;
    auto *item = new TrayItem(m_bus, service, path, this);
    if (!item->isValid()) {
        qCDebug(lcDumbar) << "tray item is invalid and will be destroyed" << "item=" << itemId;
        item->deleteLater();
        return;
    }

    m_items.insert(itemId, item);
    m_serviceWatcher->addWatchedService(service);
    connect(item, &TrayItem::changed, this, &Tray::itemChanged);
    connect(item, &TrayItem::invalid, this, &Tray::itemInvalid);

    auto *button = new TrayButton(item, this);
    m_buttons.insert(itemId, button);
    m_layout->addWidget(button);
    updateVisibility();
    qCDebug(lcDumbar) << "tray item created"
                      << "item=" << itemId
                      << "status=" << item->status()
                      << "passive=" << item->isPassive()
                      << "itemCount=" << m_items.size();
}

void Tray::unregisterItem(const QString &itemId)
{
    removeItem(itemId);
}

void Tray::removeItemsForService(const QString &service)
{
    qCDebug(lcDumbar) << "removing tray items for service" << "service=" << service;
    QStringList itemIds;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it.value()->service() == service)
            itemIds.append(it.key());
    }

    if (auto *adaptor = qobject_cast<StatusNotifierWatcherAdaptor *>(m_localWatcher))
        adaptor->removeService(service);
    for (const QString &itemId : itemIds)
        removeItem(itemId);
}

QStringList Tray::registeredItems() const
{
    return m_items.keys();
}

void Tray::itemChanged(TrayItem *item)
{
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it.value() != item)
            continue;
        if (auto *button = static_cast<TrayButton *>(m_buttons.value(it.key())))
            button->refresh();
        updateVisibility();
        return;
    }
}

void Tray::itemInvalid(TrayItem *item)
{
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it.value() == item) {
            qCDebug(lcDumbar) << "tray item became invalid" << "item=" << it.key();
            removeItem(it.key());
            return;
        }
    }
}

void Tray::serviceUnregistered(const QString &service)
{
    qCDebug(lcDumbar) << "watching service removal" << "service=" << service;
    removeItemsForService(service);
    m_serviceWatcher->removeWatchedService(service);
}

void Tray::connectToWatcher()
{
    m_bus.connect(QString::fromLatin1(kWatcherService),
                  QString::fromLatin1(kWatcherPath),
                  QString::fromLatin1(kWatcherInterface),
                  QStringLiteral("StatusNotifierItemRegistered"),
                  this,
                  SLOT(registerItem(QString)));
    m_bus.connect(QString::fromLatin1(kWatcherService),
                  QString::fromLatin1(kWatcherPath),
                  QString::fromLatin1(kWatcherInterface),
                  QStringLiteral("StatusNotifierItemUnregistered"),
                  this,
                  SLOT(unregisterItem(QString)));

    QDBusInterface watcher(QString::fromLatin1(kWatcherService),
                           QString::fromLatin1(kWatcherPath),
                           QString::fromLatin1(kWatcherInterface),
                           m_bus);
    watcher.call(QStringLiteral("RegisterStatusNotifierHost"), m_bus.baseService());
    loadRegisteredItems();
}

void Tray::setupLocalWatcher()
{
    auto *adaptor = new StatusNotifierWatcherAdaptor(this);
    m_localWatcher = adaptor;
    if (!m_bus.registerObject(QString::fromLatin1(kWatcherPath), this, QDBusConnection::ExportAdaptors)) {
        qWarning("dumbar: could not export the local StatusNotifierWatcher");
        m_bus.unregisterService(QString::fromLatin1(kWatcherService));
        m_ownsWatcher = false;
        return;
    }
    adaptor->RegisterStatusNotifierHost(m_bus.baseService());
}

void Tray::loadRegisteredItems()
{
    QDBusInterface properties(QString::fromLatin1(kWatcherService),
                              QString::fromLatin1(kWatcherPath),
                              QString::fromLatin1(kPropertiesInterface),
                              m_bus);
    const QDBusMessage reply = properties.call(QStringLiteral("Get"),
                                               QString::fromLatin1(kWatcherInterface),
                                               QStringLiteral("RegisteredStatusNotifierItems"));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return;
    for (const QString &itemId : stringListFromValue(reply.arguments().constFirst()))
        registerItem(itemId);
}

void Tray::removeItem(const QString &itemId)
{
    TrayItem *item = m_items.take(itemId);
    QWidget *button = m_buttons.take(itemId);
    if (item || button) {
        qCDebug(lcDumbar) << "destroying tray item"
                          << "item=" << itemId
                          << "status=" << (item ? item->status() : QStringLiteral("<unknown>"))
                          << "remaining=" << m_items.size();
    }
    if (button) {
        m_layout->removeWidget(button);
        button->deleteLater();
    }
    if (item)
        item->deleteLater();
    updateVisibility();
}

void Tray::updateVisibility()
{
    bool visible = false;
    for (TrayItem *item : m_items) {
        if (!item->isPassive()) {
            visible = true;
            break;
        }
    }
    setVisible(visible);
}

#include "tray.moc"
