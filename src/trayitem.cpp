#include "trayitem.h"

#include "layershell/layershelltooltip.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDateTime>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtEndian>

#include <limits>
#include <utility>

namespace
{
constexpr auto kItemInterface = "org.kde.StatusNotifierItem";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

QVariant unwrap(const QVariant &value)
{
    QVariant result = value;
    while (result.metaType() == QMetaType::fromType<QDBusVariant>())
        result = result.value<QDBusVariant>().variant();
    return result;
}

QVariant property(const QVariantMap &properties, const char *name)
{
    QVariant result = unwrap(properties.value(QString::fromLatin1(name)));
    if (result.metaType() == QMetaType::fromType<QDBusArgument>())
        result = qdbus_cast<QVariant>(result.value<QDBusArgument>());
    return result;
}

QVariantMap propertiesFromReply(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return {};

    const QVariant value = unwrap(reply.arguments().constFirst());
    if (value.metaType() == QMetaType::fromType<QVariantMap>())
        return value.toMap();
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return {};
}

QImage imageFromPixmap(const QVariant &value)
{
    const QVariant unboxed = unwrap(value);
    if (unboxed.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};

    const QDBusArgument argument = unboxed.value<QDBusArgument>();
    QImage result;
    int bestScore = std::numeric_limits<int>::max();
    argument.beginArray();
    while (!argument.atEnd()) {
        argument.beginStructure();
        int width = 0;
        int height = 0;
        QByteArray pixels;
        argument >> width >> height >> pixels;
        argument.endStructure();

        if (width <= 0 || height <= 0 || pixels.size() < width * height * 4)
            continue;

        const int score = qAbs(width - 18) + qAbs(height - 18);
        if (score >= bestScore)
            continue;

        QImage image(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto *pixel = reinterpret_cast<const uchar *>(pixels.constData()) +
                                    (static_cast<qsizetype>(y) * width + x) * 4;
                image.setPixel(x, y, qFromBigEndian<quint32>(pixel));
            }
        }
        result = image;
        bestScore = score;
    }
    argument.endArray();
    return result;
}

QIcon icon(const QString &name, const QVariant &pixmap)
{
    QIcon result = QIcon::fromTheme(name);
    if (result.isNull()) {
        const QImage image = imageFromPixmap(pixmap);
        if (!image.isNull())
            result = QIcon(QPixmap::fromImage(image));
    }
    return result;
}

QString toolTip(const QVariant &value)
{
    const QVariant unboxed = unwrap(value);
    if (unboxed.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};

    const QDBusArgument argument = unboxed.value<QDBusArgument>();
    QString title;
    QString description;
    argument.beginStructure();
    QString ignoredIconName;
    argument >> ignoredIconName;
    argument.beginArray();
    while (!argument.atEnd()) {
        argument.beginStructure();
        int width = 0;
        int height = 0;
        QByteArray ignoredPixels;
        argument >> width >> height >> ignoredPixels;
        argument.endStructure();
    }
    argument.endArray();
    argument >> title >> description;
    argument.endStructure();
    Q_UNUSED(ignoredIconName)

    if (title.isEmpty())
        return description;
    if (description.isEmpty())
        return title;
    return title + QLatin1Char('\n') + description;
}

QString objectPath(const QVariant &value)
{
    QVariant unboxed = unwrap(value);
    if (unboxed.metaType() == QMetaType::fromType<QDBusArgument>())
        unboxed = qdbus_cast<QVariant>(unboxed.value<QDBusArgument>());
    if (unboxed.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return unboxed.value<QDBusObjectPath>().path();
    return unboxed.toString();
}
}

TrayItem::TrayItem(const QDBusConnection &bus, QString service, QString path, QWidget *parent)
    : QToolButton(parent), m_bus(bus), m_service(std::move(service)), m_path(std::move(path))
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setIconSize(QSize(18, 18));
    setFixedSize(22, 24);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setToolTip(QString());
    m_tooltip = new LayerShellTooltip(this, this);

    for (const auto signal : {"NewIcon", "NewAttentionIcon", "NewStatus", "NewToolTip", "NewTitle"})
        m_bus.connect(m_service, m_path, QString::fromLatin1(kItemInterface), signal, this, SLOT(refresh()));
    refresh();
}

TrayItem::~TrayItem()
{
    m_tooltip->close();
}

void TrayItem::refresh()
{
    QDBusInterface properties(m_service, m_path, QString::fromLatin1(kPropertiesInterface), m_bus);
    const QDBusMessage reply = properties.call(QStringLiteral("GetAll"), QString::fromLatin1(kItemInterface));
    const QVariantMap values = propertiesFromReply(reply);
    if (values.isEmpty()) {
        m_valid = false;
        emit invalid();
        return;
    }
    applyProperties(values);
}

void TrayItem::applyProperties(const QVariantMap &properties)
{
    m_status = ::property(properties, "Status").toString();
    if (m_status.isEmpty())
        m_status = QStringLiteral("Active");

    const QString iconName = ::property(properties, "IconName").toString();
    const QString attentionIconName = ::property(properties, "AttentionIconName").toString();
    const QVariant iconPixmap = properties.value(QStringLiteral("IconPixmap"));
    const QVariant attentionPixmap = properties.value(QStringLiteral("AttentionIconPixmap"));
    QIcon itemIcon =
        m_status == QLatin1String("NeedsAttention") ? ::icon(attentionIconName, attentionPixmap) : QIcon();
    if (itemIcon.isNull())
        itemIcon = ::icon(iconName, iconPixmap);
    if (itemIcon.isNull())
        itemIcon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    setIcon(itemIcon);

    QString tip = ::toolTip(properties.value(QStringLiteral("ToolTip")));
    if (tip.isEmpty())
        tip = ::property(properties, "IconAccessibleDesc").toString();
    if (tip.isEmpty())
        tip = ::property(properties, "Title").toString();
    m_tooltip->setText(isPassive() ? QString() : tip);

    m_itemIsMenu = ::property(properties, "ItemIsMenu").toBool();
    m_menuPath = objectPath(properties.value(QStringLiteral("Menu")));
    m_valid = true;
    setVisible(!isPassive());
    emit updated();
}

void TrayItem::invoke(const QString &method, const QVariantList &arguments) const
{
    QDBusInterface item(m_service, m_path, QString::fromLatin1(kItemInterface), m_bus);
    item.callWithArgumentList(QDBus::AutoDetect, method, arguments);
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
    const quint32 timestamp = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch());
    callMenu(QStringLiteral("Event"), {
                                          itemId,
                                          QStringLiteral("clicked"),
                                          QVariant::fromValue(QDBusVariant(QVariant(0))),
                                          timestamp,
                                      });
}

void TrayItem::mousePressEvent(QMouseEvent *event)
{
    const QPoint point = event->globalPosition().toPoint();
    const auto button = event->button();
    if ((button == Qt::LeftButton && m_itemIsMenu && hasMenu()) || (button == Qt::RightButton && hasMenu())) {
        m_tooltip->close();
        emit menuRequested();
    } else if (button == Qt::LeftButton) {
        invoke(QStringLiteral("Activate"), {point.x(), point.y()});
    } else if (button == Qt::MiddleButton) {
        invoke(QStringLiteral("SecondaryActivate"), {point.x(), point.y()});
    } else if (button == Qt::RightButton) {
        invoke(QStringLiteral("ContextMenu"), {point.x(), point.y()});
    }
    event->accept();
}

void TrayItem::wheelEvent(QWheelEvent *event)
{
    if (const int delta = event->angleDelta().y())
        invoke(QStringLiteral("Scroll"), {delta, QStringLiteral("vertical")});
    event->accept();
}
