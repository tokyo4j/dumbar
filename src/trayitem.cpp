#include "trayitem.h"

#include "dumbar.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDateTime>
#include <QImage>
#include <QPainter>
#include <QtEndian>

#include <limits>

namespace
{
constexpr auto kItemInterface = "org.kde.StatusNotifierItem";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

QVariant unbox(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariant>(value.value<QDBusArgument>());
    return value;
}

QVariant unwrapVariants(const QVariant &value)
{
    QVariant result = value;
    while (result.metaType() == QMetaType::fromType<QDBusVariant>())
        result = result.value<QDBusVariant>().variant();
    return result;
}

QVariantMap propertiesFromReply(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return {};
    const QVariant value = reply.arguments().constFirst();
    if (value.metaType() == QMetaType::fromType<QVariantMap>())
        return value.toMap();
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return {};
}

QImage imageFromPixmapValue(const QVariant &value)
{
    const QVariant unboxed = unbox(value);
    if (unboxed.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};

    const QDBusArgument argument = unboxed.value<QDBusArgument>();
    QImage bestImage;
    int bestScore = std::numeric_limits<int>::max();
    argument.beginArray();
    while (!argument.atEnd()) {
        argument.beginStructure();
        int width = 0;
        int height = 0;
        QByteArray pixels;
        argument >> width >> height >> pixels;
        argument.endStructure();

        if (width > 0 && height > 0 && pixels.size() >= width * height * 4) {
            const int score = qAbs(width - 18) + qAbs(height - 18);
            if (score < bestScore) {
                QImage image(width, height, QImage::Format_ARGB32);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const auto *pixel = reinterpret_cast<const uchar *>(pixels.constData())
                            + (static_cast<qsizetype>(y) * width + x) * 4;
                        image.setPixel(x, y, qFromBigEndian<quint32>(pixel));
                    }
                }
                bestImage = image;
                bestScore = score;
            }
        }
    }
    argument.endArray();
    return bestImage;
}

QString toolTipFromValue(const QVariant &value)
{
    // ToolTip is a structured SNI property.  Only unwrap the D-Bus variant
    // wrapper here; treating the structure itself as a variant loses it.
    const QVariant unboxed = unwrapVariants(value);
    if (unboxed.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};

    const QDBusArgument argument = unboxed.value<QDBusArgument>();
    QString iconName;
    QString title;
    QString description;
    argument.beginStructure();
    argument >> iconName;
    argument.beginArray();
    while (!argument.atEnd()) {
        argument.beginStructure();
        int width = 0;
        int height = 0;
        QByteArray pixels;
        argument >> width >> height >> pixels;
        argument.endStructure();
    }
    argument.endArray();
    argument >> title >> description;
    argument.endStructure();
    Q_UNUSED(iconName)
    if (title.isEmpty())
        return description;
    if (description.isEmpty())
        return title;
    return title + QStringLiteral("\n") + description;
}

QString objectPathFromValue(const QVariant &value)
{
    const QVariant unboxed = unbox(value);
    if (unboxed.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return unboxed.value<QDBusObjectPath>().path();
    return unboxed.toString();
}

QIcon iconFromValues(const QString &name, const QVariant &pixmap)
{
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull())
        return icon;
    const QImage image = imageFromPixmapValue(pixmap);
    if (!image.isNull())
        return QIcon(QPixmap::fromImage(image));
    return {};
}

}

TrayItem::TrayItem(const QDBusConnection &bus,
                   const QString &service,
                   const QString &path,
                   QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_service(service)
    , m_path(path)
{
    qCDebug(lcDumbar) << "constructing tray item"
                      << "service=" << m_service
                      << "path=" << m_path;
    m_bus.connect(m_service,
                  m_path,
                  QString::fromLatin1(kItemInterface),
                  QStringLiteral("NewIcon"),
                  this,
                  SLOT(refresh()));
    m_bus.connect(m_service,
                  m_path,
                  QString::fromLatin1(kItemInterface),
                  QStringLiteral("NewAttentionIcon"),
                  this,
                  SLOT(refresh()));
    m_bus.connect(m_service,
                  m_path,
                  QString::fromLatin1(kItemInterface),
                  QStringLiteral("NewStatus"),
                  this,
                  SLOT(refresh()));
    m_bus.connect(m_service,
                  m_path,
                  QString::fromLatin1(kItemInterface),
                  QStringLiteral("NewToolTip"),
                  this,
                  SLOT(refresh()));
    m_bus.connect(m_service,
                  m_path,
                  QString::fromLatin1(kItemInterface),
                  QStringLiteral("NewTitle"),
                  this,
                  SLOT(refresh()));
    refresh();
    qCDebug(lcDumbar) << "tray item initialized"
                      << "item=" << id()
                      << "valid=" << m_valid
                      << "status=" << m_status
                      << "iconResolved=" << !m_icon.isNull();
}

TrayItem::~TrayItem()
{
    qCDebug(lcDumbar) << "tray item destroyed" << "item=" << id();
}

void TrayItem::refresh()
{
    QDBusInterface properties(m_service,
                              m_path,
                              QString::fromLatin1(kPropertiesInterface),
                              m_bus);
    const QDBusMessage reply = properties.call(QStringLiteral("GetAll"), QString::fromLatin1(kItemInterface));
    const QVariantMap values = propertiesFromReply(reply);
    if (values.isEmpty()) {
        m_valid = false;
        emit invalid(this);
        return;
    }
    applyProperties(values);
}

void TrayItem::applyProperties(const QVariantMap &properties)
{
    m_status = unbox(properties.value(QStringLiteral("Status"))).toString();
    if (m_status.isEmpty())
        m_status = QStringLiteral("Active");

    const QString iconName = unbox(properties.value(QStringLiteral("IconName"))).toString();
    const QString attentionIconName = unbox(properties.value(QStringLiteral("AttentionIconName"))).toString();
    const QVariant iconPixmap = properties.value(QStringLiteral("IconPixmap"));
    const QVariant attentionPixmap = properties.value(QStringLiteral("AttentionIconPixmap"));

    if (m_status == QLatin1String("NeedsAttention")) {
        m_icon = iconFromValues(attentionIconName, attentionPixmap);
        if (m_icon.isNull())
            m_icon = iconFromValues(iconName, iconPixmap);
    } else {
        m_icon = iconFromValues(iconName, iconPixmap);
    }

    if (m_icon.isNull())
        m_icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));

    qCDebug(lcDumbar) << "tray item icon"
                      << "item=" << id()
                      << "status=" << m_status
                      << "iconName=" << iconName
                      << "attentionIconName=" << attentionIconName
                      << "iconPixmapValid=" << iconPixmap.isValid()
                      << "attentionPixmapValid=" << attentionPixmap.isValid()
                      << "theme=" << QIcon::themeName()
                      << "resolved=" << !m_icon.isNull();

    if (m_icon.isNull()) {
        if (!m_missingIconWarningIssued) {
            qCWarning(lcDumbar) << "tray item has no usable icon"
                                << "item=" << id()
                                << "iconName=" << iconName
                                << "attentionIconName=" << attentionIconName
                                << "iconPixmapValid=" << iconPixmap.isValid()
                                << "attentionPixmapValid=" << attentionPixmap.isValid()
                                << "theme=" << QIcon::themeName();
            m_missingIconWarningIssued = true;
        }
    } else if (m_missingIconWarningIssued) {
        qCDebug(lcDumbar) << "tray item icon became available" << "item=" << id();
        m_missingIconWarningIssued = false;
    }

    m_toolTip = toolTipFromValue(properties.value(QStringLiteral("ToolTip")));
    if (m_toolTip.isEmpty())
        m_toolTip = unbox(properties.value(QStringLiteral("IconAccessibleDesc"))).toString();
    if (m_toolTip.isEmpty())
        m_toolTip = unbox(properties.value(QStringLiteral("Title"))).toString();
    m_itemIsMenu = unbox(properties.value(QStringLiteral("ItemIsMenu"))).toBool();
    m_menuPath = objectPathFromValue(properties.value(QStringLiteral("Menu")));
    m_valid = true;
    emit changed(this);
}

void TrayItem::activate(int x, int y)
{
    QDBusInterface item(m_service, m_path, QString::fromLatin1(kItemInterface), m_bus);
    item.call(QStringLiteral("Activate"), x, y);
}

void TrayItem::secondaryActivate(int x, int y)
{
    QDBusInterface item(m_service, m_path, QString::fromLatin1(kItemInterface), m_bus);
    item.call(QStringLiteral("SecondaryActivate"), x, y);
}

void TrayItem::scroll(int delta, const QString &orientation)
{
    QDBusInterface item(m_service, m_path, QString::fromLatin1(kItemInterface), m_bus);
    item.call(QStringLiteral("Scroll"), delta, orientation);
}

void TrayItem::contextMenu(int x, int y)
{
    QDBusInterface item(m_service, m_path, QString::fromLatin1(kItemInterface), m_bus);
    item.call(QStringLiteral("ContextMenu"), x, y);
}

QDBusMessage TrayItem::callMenu(const QString &method, const QVariantList &arguments) const
{
    if (!hasMenu())
        return {};

    QDBusInterface menu(m_service, m_menuPath, QStringLiteral("com.canonical.dbusmenu"), m_bus);
    menu.setTimeout(1000);
    return menu.callWithArgumentList(QDBus::Block, method, arguments);
}

void TrayItem::menuEvent(int itemId) const
{
    // com.canonical.dbusmenu uses the same millisecond timestamp convention
    // as a native pointer event.  The protocol field is uint32 and therefore
    // intentionally wraps just like the toolkit event timestamps do.
    const quint32 timestamp = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch());
    const QVariantList arguments {
        itemId,
        QStringLiteral("clicked"),
        QVariant::fromValue(QDBusVariant(QVariant(0))),
        timestamp,
    };
    callMenu(QStringLiteral("Event"), arguments);
}
