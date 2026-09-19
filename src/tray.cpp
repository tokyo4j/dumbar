#include "tray.h"

#include "dumbar.h"
#include "layershell/layershellmenu.h"
#include "trayitem.h"
#include "layershell/layershelltooltip.h"

#include <QDBusArgument>
#include <QDBusAbstractAdaptor>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QActionGroup>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMenu>
#include <QToolButton>
#include <QVariant>
#include <QWheelEvent>

#include <utility>

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

QVariant unwrapMenuVariant(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

struct MenuNode
{
    int id = 0;
    QVariantMap properties;
    QList<MenuNode> children;
};

QVariant menuProperty(const QVariantMap &properties, const char *name)
{
    return unwrapMenuVariant(properties.value(QString::fromLatin1(name)));
}

QString menuString(const QVariantMap &properties, const char *name)
{
    return menuProperty(properties, name).toString();
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
        value = unwrapMenuVariant(value);
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

    QVariant value = unwrapMenuVariant(reply.arguments().at(1));
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return false;

    const QDBusArgument argument = value.value<QDBusArgument>();
    int nodeCount = 0;
    return parseMenuNode(argument, root, 0, &nodeCount);
}

bool parseBooleanReply(const QDBusMessage &reply, bool *value)
{
    if (!value || reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return false;

    QVariant argumentValue = unwrapMenuVariant(reply.arguments().constFirst());
    if (argumentValue.metaType() != QMetaType::fromType<QDBusArgument>()) {
        *value = argumentValue.toBool();
        return true;
    }

    const QDBusArgument argument = argumentValue.value<QDBusArgument>();
    argument.beginStructure();
    argument >> *value;
    argument.endStructure();
    return true;
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

void populateMenu(QMenu *menu, const MenuNode &node, TrayItem *item);

void refreshSubmenu(QMenu *menu, TrayItem *item, int itemId)
{
    if (!menu || !item)
        return;

    const QDBusMessage aboutToShow = item->callMenu(QStringLiteral("AboutToShow"), { itemId });
    bool needsRefresh = false;
    if (!parseBooleanReply(aboutToShow, &needsRefresh) || !needsRefresh)
        return;

    const QDBusMessage layout = item->callMenu(QStringLiteral("GetLayout"),
                                                { itemId, -1, QStringList() });
    MenuNode node;
    if (!parseMenuLayout(layout, &node)) {
        qCWarning(lcDumbar) << "could not refresh tray menu submenu"
                            << "item=" << item->id()
                            << "menuItem=" << itemId;
        return;
    }

    menu->clear();
    populateMenu(menu, node, item);
}

void populateMenu(QMenu *menu, const MenuNode &node, TrayItem *item)
{
    if (!menu || !item)
        return;

    QActionGroup *radioGroup = nullptr;
    const QPointer<TrayItem> itemGuard(item);
    for (const MenuNode &child : node.children) {
        if (!menuBool(child.properties, "visible", true))
            continue;

        if (menuString(child.properties, "type") == QLatin1String("separator")) {
            menu->addSeparator();
            continue;
        }

        const QString label = menuString(child.properties, "label");
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

        const bool hasSubmenu = !child.children.isEmpty()
                                || menuString(child.properties, "children-display")
                                    == QLatin1String("submenu");
        if (hasSubmenu) {
            auto *submenu = new QMenu(menu);
            submenu->setTitle(label);
            submenu->setIcon(action->icon());
            submenu->setStyleSheet(menu->styleSheet());
            submenu->setSeparatorsCollapsible(false);
            action->setMenu(submenu);
            populateMenu(submenu, child, item);

            QObject::connect(submenu, &QMenu::aboutToShow, submenu,
                             [submenu, itemGuard, itemId = child.id] {
                if (itemGuard)
                    refreshSubmenu(submenu, itemGuard.data(), itemId);
            });
        } else {
            QObject::connect(action, &QAction::triggered, action, [itemGuard, itemId = child.id] {
                if (itemGuard)
                    itemGuard->menuEvent(itemId);
            });
        }
    }
}

class TrayButton final : public QToolButton
{
public:
    TrayButton(TrayItem *item, Tray *tray, QWidget *parent)
        : QToolButton(parent)
        , m_item(item)
        , m_tray(tray)
    {
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        setIconSize(QSize(18, 18));
        setFixedSize(22, 24);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        refresh();
    }

    void refresh()
    {
        setIcon(m_item->icon());
        // Native Qt tooltips are independent top-level surfaces on Wayland.
        // Tray owns a delayed tooltip so it can attach it to the panel's
        // layer-shell surface and position it in panel-local coordinates.
        setToolTip(QString());
        setVisible(!m_item->isPassive());
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QPoint point = event->globalPosition().toPoint();
        if (event->button() == Qt::LeftButton) {
            if (m_item->itemIsMenu() && m_item->hasMenu())
                m_tray->showPopup(m_item, this);
            else
                m_item->activate(point.x(), point.y());
        } else if (event->button() == Qt::MiddleButton) {
            m_item->secondaryActivate(point.x(), point.y());
        } else if (event->button() == Qt::RightButton) {
            if (m_item->hasMenu())
                m_tray->showPopup(m_item, this);
            else
                m_item->contextMenu(point.x(), point.y());
        }
        event->accept();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            m_item->scroll(delta, QStringLiteral("vertical"));
        event->accept();
    }

    void enterEvent(QEnterEvent *event) override
    {
        QToolButton::enterEvent(event);
        qCDebug(lcDumbar) << "tray tooltip hover enter"
                          << "item=" << m_item->id()
                          << "text=" << m_item->toolTip();
        m_tray->showTooltip(this, m_item->toolTip());
    }

    void leaveEvent(QEvent *event) override
    {
        m_tray->hideTooltip(this);
        QToolButton::leaveEvent(event);
    }

private:
    TrayItem *m_item = nullptr;
    Tray *m_tray = nullptr;
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
    m_tooltip = new LayerShellTooltip(this);

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
    m_tooltip->close();
    if (m_popup)
        m_popup->close();
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

    auto *button = new TrayButton(item, this, this);
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

void Tray::showTooltip(QWidget *button, const QString &text)
{
    m_tooltip->show(button, text);
}

void Tray::hideTooltip(QWidget *button)
{
    m_tooltip->hide(button);
}

void Tray::showPopup(TrayItem *item, QWidget *button)
{
    if (!item || !button || !item->hasMenu())
        return;

    m_tooltip->close();
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
    item->callMenu(QStringLiteral("AboutToShow"), { 0 });
    const QDBusMessage layout = item->callMenu(QStringLiteral("GetLayout"),
                                                { 0, -1, QStringList() });
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

    if (!menu->popupFor(button)) {
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
                      << "item=" << item->id()
                      << "actionCount=" << menu->actions().size();
}

void Tray::itemChanged(TrayItem *item)
{
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it.value() != item)
            continue;
        if (auto *button = static_cast<TrayButton *>(m_buttons.value(it.key()))) {
            button->refresh();
            if (m_tooltip->isFor(button)) {
                if (item->toolTip().isEmpty() || item->isPassive()) {
                    hideTooltip(button);
                } else {
                    m_tooltip->update(button, item->toolTip());
                }
            }
        }
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
    if (button)
        hideTooltip(button);
    if (item && m_popupItem == item) {
        if (m_popup)
            m_popup->close();
        m_popup = nullptr;
        m_popupItem = nullptr;
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
