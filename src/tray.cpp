#include "tray.h"

#include "dumbar.h"
#include "layershell/layershellmenu.h"

#include <QActionGroup>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QHBoxLayout>
#include <QImage>
#include <QMenu>
#include <QVariant>

#include <utility>

namespace
{
constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
constexpr auto kWatcherInterface = "org.kde.StatusNotifierWatcher";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

QVariant unwrap(const QVariant &value)
{
    QVariant result = value;
    while (result.metaType() == QMetaType::fromType<QDBusVariant>())
        result = result.value<QDBusVariant>().variant();
    return result;
}

QStringList stringListFromValue(const QVariant &value)
{
    const QVariant unboxed = unwrap(value);
    if (unboxed.metaType() == QMetaType::fromType<QStringList>())
        return unboxed.toStringList();
    if (unboxed.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QStringList>(unboxed.value<QDBusArgument>());
    return {};
}

struct MenuNode
{
    int id = 0;
    QVariantMap properties;
    QList<MenuNode> children;
};

QVariant menuProperty(const QVariantMap &properties, const char *name)
{
    return unwrap(properties.value(QString::fromLatin1(name)));
}

QString menuString(const QVariantMap &properties, const char *name)
{
    return menuProperty(properties, name).toString();
}

QString dbusMenuLabelToQtText(const QString &label)
{
    // DBusMenu labels use '_' for mnemonics and '__' for a literal
    // underscore.  Qt uses '&' for mnemonics and '&&' for a literal
    // ampersand, so translate the label at the toolkit boundary.
    QString result;
    result.reserve(label.size() + 1);
    bool mnemonicAdded = false;

    for (qsizetype i = 0; i < label.size(); ++i) {
        const QChar character = label.at(i);
        if (character == QLatin1Char('_')) {
            if (i + 1 < label.size() && label.at(i + 1) == QLatin1Char('_')) {
                result += QLatin1Char('_');
                ++i;
            } else if (!mnemonicAdded && i + 1 < label.size()) {
                result += QLatin1Char('&');
                mnemonicAdded = true;
            }
            continue;
        }

        if (character == QLatin1Char('&'))
            result += QStringLiteral("&&");
        else
            result += character;
    }

    return result;
}

bool menuBool(const QVariantMap &properties, const char *name, bool defaultValue)
{
    const QVariant value = menuProperty(properties, name);
    return value.isValid() ? value.toBool() : defaultValue;
}

QIcon menuIcon(const QVariantMap &properties)
{
    QIcon icon = QIcon::fromTheme(menuString(properties, "icon-name"));
    if (!icon.isNull())
        return icon;

    const QVariant value = menuProperty(properties, "icon-data");
    QByteArray data;
    if (value.metaType() == QMetaType::fromType<QByteArray>()) {
        data = value.toByteArray();
    } else if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        data = qdbus_cast<QByteArray>(value.value<QDBusArgument>());
    }
    if (data.isEmpty())
        return {};

    const QImage image = QImage::fromData(data);
    return image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image));
}

bool parseMenuNode(const QDBusArgument &argument, MenuNode *node, int depth, int *nodeCount)
{
    if (!node || !nodeCount || depth > 32 || ++*nodeCount > 4096)
        return false;

    argument.beginStructure();
    argument >> node->id;
    argument >> node->properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QVariant value;
        argument >> value;
        value = unwrap(value);
        if (value.metaType() != QMetaType::fromType<QDBusArgument>()) {
            argument.endArray();
            argument.endStructure();
            return false;
        }

        MenuNode child;
        if (!parseMenuNode(value.value<QDBusArgument>(), &child, depth + 1, nodeCount)) {
            argument.endArray();
            argument.endStructure();
            return false;
        }
        node->children.append(std::move(child));
    }
    argument.endArray();
    argument.endStructure();
    return true;
}

bool parseMenuLayout(const QDBusMessage &reply, MenuNode *root)
{
    // GetLayout returns two top-level values: the layout revision followed by
    // the root menu item.  QDBusMessage keeps both values as separate
    // arguments; the revision is not part of the layout structure.
    if (!root || reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 2)
        return false;

    QVariant value = unwrap(reply.arguments().at(1));
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return false;

    const QDBusArgument argument = value.value<QDBusArgument>();
    int nodeCount = 0;
    return parseMenuNode(argument, root, 0, &nodeCount);
}

class StatusNotifierWatcherAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit StatusNotifierWatcherAdaptor(Tray *tray)
        : QDBusAbstractAdaptor(tray)
        , m_tray(tray)
    {
    }

    QStringList registeredItems() const { return m_tray->registeredItems(); }
    bool isHostRegistered() const { return m_hostRegistered; }
    int protocolVersion() const { return 0; }

    void removeService(const QString &service)
    {
        qCDebug(lcDumbar) << "status notifier service unregistered"
                          << "service=" << service;
        const QStringList items = m_tray->registeredItems();
        for (const QString &item : items) {
            const QString itemService = item.section(QLatin1Char('/'), 0, 0);
            if (itemService != service)
                continue;
            emit StatusNotifierItemUnregistered(item);
        }
    }

public slots:
    void RegisterStatusNotifierItem(const QString &service)
    {
        const QStringList before = m_tray->registeredItems();
        const QString itemId = m_tray->registerItem(service);
        if (itemId.isEmpty() || before.contains(itemId))
            return;
        qCDebug(lcDumbar) << "status notifier item registered"
                          << "registration=" << service << "item=" << itemId;
        emit StatusNotifierItemRegistered(itemId);
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
    bool m_hostRegistered = false;
};

void populateMenu(QMenu *menu, const MenuNode &node, TrayItem *item);

void populateMenu(QMenu *menu, const MenuNode &node, TrayItem *item)
{
    QActionGroup *radioGroup = nullptr;
    for (const MenuNode &child : node.children) {
        if (!menuBool(child.properties, "visible", true))
            continue;

        if (menuString(child.properties, "type") == QLatin1String("separator")) {
            menu->addSeparator();
            continue;
        }

        const QString label = dbusMenuLabelToQtText(menuString(child.properties, "label"));
        QAction *action = menu->addAction(menuIcon(child.properties), label);
        action->setEnabled(menuBool(child.properties, "enabled", true));

        const QString toggleType = menuString(child.properties, "toggle-type");
        if (toggleType == QLatin1String("checkmark") || toggleType == QLatin1String("radio")) {
            action->setCheckable(true);
            action->setChecked(menuProperty(child.properties, "toggle-state").toInt() != 0);
            if (toggleType == QLatin1String("radio")) {
                if (!radioGroup) {
                    radioGroup = new QActionGroup(menu);
                    radioGroup->setExclusive(true);
                }
                radioGroup->addAction(action);
            }
        }

        const bool hasSubmenu = !child.children.isEmpty() ||
                                menuString(child.properties, "children-display") == QLatin1String("submenu");
        if (hasSubmenu) {
            auto *submenu = new QMenu(menu);
            submenu->setTitle(label);
            submenu->setIcon(action->icon());
            submenu->setStyleSheet(menu->styleSheet());
            submenu->setSeparatorsCollapsible(false);
            action->setMenu(submenu);
            populateMenu(submenu, child, item);

        } else {
            QObject::connect(action, &QAction::triggered, item,
                             [item, itemId = child.id] { item->menuEvent(itemId); });
        }
    }
}

}

Tray::Tray(QWidget *parent) : QWidget(parent), m_bus(QDBusConnection::sessionBus())
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(1);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    hide();

    qCDebug(lcDumbar) << "creating system tray"
                      << "dbusConnected=" << m_bus.isConnected();

    if (!m_bus.isConnected()) {
        qWarning("dumbar: session D-Bus is unavailable; system tray disabled");
        return;
    }

    m_serviceWatcher = new QDBusServiceWatcher(this);
    m_serviceWatcher->setConnection(m_bus);
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &Tray::serviceUnregistered);

    const bool watcherExists =
        m_bus.interface() &&
        m_bus.interface()->isServiceRegistered(QString::fromLatin1(kWatcherService)).value();
    if (watcherExists) {
        connectToWatcher();
    } else if (m_bus.registerService(QString::fromLatin1(kWatcherService))) {
        m_ownsWatcher = true;
        setupLocalWatcher();
    } else if (m_bus.interface() &&
               m_bus.interface()->isServiceRegistered(QString::fromLatin1(kWatcherService)).value()) {
        connectToWatcher();
    } else {
        qWarning("dumbar: could not provide or find a StatusNotifierWatcher; "
                 "system tray disabled");
    }
}

Tray::~Tray()
{
    qCDebug(lcDumbar) << "destroying system tray"
                      << "itemCount=" << m_items.size();
    closePopup();
    if (m_ownsWatcher) {
        m_bus.unregisterObject(QString::fromLatin1(kWatcherPath));
        m_bus.unregisterService(QString::fromLatin1(kWatcherService));
    }
}

QString Tray::registerItem(const QString &registration)
{
    if (registration.isEmpty())
        return {};

    QString itemId = registration;
    if (registration.startsWith(QLatin1Char('/'))) {
        if (!calledFromDBus() || message().service().isEmpty()) {
            qCWarning(lcDumbar) << "ignoring path-only tray item without a D-Bus sender"
                                << "path=" << registration;
            return {};
        }
        // The SNI protocol allows an item to register only its object path;
        // in that form the caller's unique bus name identifies the service.
        itemId = message().service() + registration;
    }

    if (m_items.contains(itemId)) {
        qCDebug(lcDumbar) << "ignoring duplicate tray item" << "item=" << itemId;
        return itemId;
    }

    const int slash = itemId.indexOf(QLatin1Char('/'));
    const QString service = slash < 0 ? itemId : itemId.left(slash);
    const QString path = slash < 0 ? QStringLiteral("/StatusNotifierItem") : itemId.mid(slash);
    if (service.isEmpty() || !path.startsWith(QLatin1Char('/'))) {
        qCDebug(lcDumbar) << "ignoring malformed tray item" << "item=" << itemId;
        return {};
    }

    qCDebug(lcDumbar) << "creating tray item"
                      << "item=" << itemId << "service=" << service << "path=" << path;
    auto *item = new TrayItem(m_bus, service, path, this);
    if (!item->isValid()) {
        qCDebug(lcDumbar) << "tray item is invalid and will be destroyed"
                          << "item=" << itemId;
        item->deleteLater();
        return {};
    }

    m_items.insert(itemId, item);
    m_serviceWatcher->addWatchedService(service);
    connect(item, &TrayItem::updated, this, &Tray::updateVisibility);
    connect(item, &TrayItem::invalid, this, [this, item] { removeItem(item->id()); });
    connect(item, &TrayItem::menuRequested, this, [this, item] { showPopup(item); });
    m_layout->addWidget(item);
    updateVisibility();
    qCDebug(lcDumbar) << "tray item created"
                      << "item=" << itemId << "status=" << item->status() << "passive=" << item->isPassive()
                      << "itemCount=" << m_items.size();
    return itemId;
}

void Tray::unregisterItem(const QString &itemId)
{
    removeItem(itemId);
}

void Tray::removeItemsForService(const QString &service)
{
    qCDebug(lcDumbar) << "removing tray items for service"
                      << "service=" << service;
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

void Tray::showPopup(TrayItem *item)
{
    if (!item || !item->hasMenu())
        return;

    if (m_popup) {
        const bool sameItem = m_popupItem == item;
        m_popup->close();
        m_popup = nullptr;
        m_popupItem = nullptr;
        if (sameItem)
            return;
    }

    // Ask the item whether the menu needs a refresh before obtaining the
    // layout.  A fresh layout on every open keeps check states and dynamic
    // tray menus in sync without keeping a stale QWidget tree around.
    item->callMenu(QStringLiteral("AboutToShow"), {0});
    const QDBusMessage layout = item->callMenu(QStringLiteral("GetLayout"), {0, -1, QStringList()});
    MenuNode root;
    if (!parseMenuLayout(layout, &root)) {
        qCWarning(lcDumbar) << "could not load tray item popup menu"
                            << "item=" << item->id();
        return;
    }

    auto *menu = new LayerShellMenu;
    populateMenu(menu, root, item);
    if (menu->actions().isEmpty()) {
        menu->deleteLater();
        return;
    }

    if (!menu->popupFor(item)) {
        menu->deleteLater();
        return;
    }

    m_popup = menu;
    m_popupItem = item;
    connect(menu, &QObject::destroyed, this, [this, menu] {
        if (m_popup.data() == menu) {
            m_popup = nullptr;
            m_popupItem = nullptr;
        }
    });

    qCDebug(lcDumbar) << "showing tray popup"
                      << "item=" << item->id() << "actionCount=" << menu->actions().size();
}

void Tray::serviceUnregistered(const QString &service)
{
    qCDebug(lcDumbar) << "watching service removal" << "service=" << service;
    removeItemsForService(service);
    m_serviceWatcher->removeWatchedService(service);
}

void Tray::connectToWatcher()
{
    m_bus.connect(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                  QString::fromLatin1(kWatcherInterface), QStringLiteral("StatusNotifierItemRegistered"),
                  this, SLOT(registerItem(QString)));
    m_bus.connect(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                  QString::fromLatin1(kWatcherInterface), QStringLiteral("StatusNotifierItemUnregistered"),
                  this, SLOT(unregisterItem(QString)));

    QDBusInterface watcher(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                           QString::fromLatin1(kWatcherInterface), m_bus);
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
    QDBusInterface properties(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                              QString::fromLatin1(kPropertiesInterface), m_bus);
    const QDBusMessage reply = properties.call(QStringLiteral("Get"), QString::fromLatin1(kWatcherInterface),
                                               QStringLiteral("RegisteredStatusNotifierItems"));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return;
    for (const QString &itemId : stringListFromValue(reply.arguments().constFirst()))
        registerItem(itemId);
}

void Tray::removeItem(const QString &itemId)
{
    TrayItem *item = m_items.take(itemId);
    if (item) {
        qCDebug(lcDumbar) << "destroying tray item"
                          << "item=" << itemId << "status=" << item->status()
                          << "remaining=" << m_items.size();
    }
    if (item && m_popupItem == item) {
        closePopup();
    }
    if (item) {
        m_layout->removeWidget(item);
        item->deleteLater();
    }
    updateVisibility();
}

void Tray::closePopup()
{
    if (m_popup)
        m_popup->close();
    m_popup = nullptr;
    m_popupItem = nullptr;
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
