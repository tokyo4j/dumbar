#include "battery.h"

#include "dumbar.h"
#include "layershell/layershelltooltip.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>

namespace
{
constexpr auto kDevicePath = "/org/freedesktop/UPower/devices/DisplayDevice";
constexpr auto kDeviceInterface = "org.freedesktop.UPower.Device";

QVariant unbox(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariant>(value.value<QDBusArgument>());
    return value;
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

QString stateName(int state)
{
    switch (state) {
    case 1:
        return QStringLiteral("Charging");
    case 2:
        return QStringLiteral("Discharging");
    case 3:
        return QStringLiteral("Empty");
    case 4:
        return QStringLiteral("Fully charged");
    case 5:
        return QStringLiteral("Pending charge");
    case 6:
        return QStringLiteral("Pending discharge");
    default:
        return QStringLiteral("Unknown state");
    }
}
}

Battery::Battery(QWidget *parent)
    : QWidget(parent)
    , m_bus(QDBusConnection::systemBus())
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_icon = new QLabel(this);
    m_icon->setFixedWidth(DumbarStyle::kIconSize);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_text = new QLabel(this);
    m_text->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_icon);
    layout->addWidget(m_text);
    m_tooltip = new LayerShellTooltip(this, this);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    if (!m_bus.isConnected()) {
        hide();
        qWarning("dumbar: system D-Bus is unavailable; battery disabled");
        m_warned = true;
        return;
    }

    if (!m_bus.connect(QStringLiteral("org.freedesktop.UPower"),
                       QString::fromLatin1(kDevicePath),
                       QStringLiteral("org.freedesktop.DBus.Properties"),
                       QStringLiteral("PropertiesChanged"),
                       this,
                       SLOT(propertiesChanged(QString, QVariantMap, QStringList)))) {
        qWarning("dumbar: could not subscribe to UPower property changes");
    }

    refresh();
}

QString Battery::formatDuration(qint64 seconds)
{
    if (seconds <= 0)
        return {};

    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    return QStringLiteral("%1m").arg(qMax<qint64>(1, minutes));
}

void Battery::propertiesChanged(const QString &interfaceName,
                                const QVariantMap &changed,
                                const QStringList &invalidated)
{
    Q_UNUSED(changed)
    Q_UNUSED(invalidated)
    if (interfaceName == QLatin1String(kDeviceInterface))
        refresh();
}

void Battery::refresh()
{
    QDBusInterface propertyInterface(QStringLiteral("org.freedesktop.UPower"),
                                      QString::fromLatin1(kDevicePath),
                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                      m_bus);
    const QDBusMessage reply = propertyInterface.call(QStringLiteral("GetAll"),
                                                      QString::fromLatin1(kDeviceInterface));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (!m_warned) {
            qWarning() << "dumbar: UPower is unavailable:" << reply.errorMessage();
            m_warned = true;
        }
        m_tooltip->setText({});
        hide();
        return;
    }

    const QVariantMap properties = propertiesFromReply(reply);
    if (properties.isEmpty()) {
        m_tooltip->setText({});
        hide();
        return;
    }
    applyProperties(properties);
}

void Battery::applyProperties(const QVariantMap &properties)
{
    if (!unbox(properties.value(QStringLiteral("IsPresent"))).toBool()) {
        m_tooltip->setText({});
        hide();
        return;
    }

    const double percentage = unbox(properties.value(QStringLiteral("Percentage"))).toDouble();
    const int state = unbox(properties.value(QStringLiteral("State"))).toInt();
    const QString iconName = unbox(properties.value(QStringLiteral("IconName"))).toString();
    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("battery-full"));

    const QPixmap pixmap = icon.pixmap(DumbarStyle::kIconSize, DumbarStyle::kIconSize);
    if (pixmap.isNull()) {
        m_icon->setText(QStringLiteral("🔋"));
    } else {
        m_icon->setText({});
        m_icon->setPixmap(pixmap);
    }

    m_text->setText(QStringLiteral("%1%").arg(qRound(percentage)));

    QString tooltip = QStringLiteral("Battery: %1%\n%2").arg(qRound(percentage)).arg(stateName(state));
    qint64 remaining = 0;
    if (state == 2)
        remaining = unbox(properties.value(QStringLiteral("TimeToEmpty"))).toLongLong();
    else if (state == 1 || state == 5)
        remaining = unbox(properties.value(QStringLiteral("TimeToFull"))).toLongLong();
    const QString duration = formatDuration(remaining);
    if (!duration.isEmpty())
        tooltip += QStringLiteral("\n%1 remaining").arg(duration);
    m_tooltip->setText(tooltip);
    show();
}
