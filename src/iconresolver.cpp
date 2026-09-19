#include "iconresolver.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace
{
constexpr auto kFallbackIcon = "application-x-executable";

void addIconKey(QHash<QString, QString> &icons, const QString &key, const QString &iconName)
{
    if (!key.isEmpty() && !iconName.isEmpty() && !icons.contains(key))
        icons.insert(key, iconName);
}
}

IconResolver::IconResolver(QObject *parent)
    : QObject(parent)
{
    scanDesktopFiles();
}

void IconResolver::scanDesktopFiles()
{
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &location : locations) {
        const QDir directory(location);
        const QFileInfoList files = directory.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files | QDir::Readable,
                                                             QDir::Name);
        for (const QFileInfo &file : files) {
            QSettings desktop(file.absoluteFilePath(), QSettings::IniFormat);
            const QString iconName = desktop.value(QStringLiteral("Desktop Entry/Icon")).toString();
            if (iconName.isEmpty() || desktop.value(QStringLiteral("Desktop Entry/Hidden"), false).toBool())
                continue;

            const QString fileName = file.fileName();
            addIconKey(m_iconNames, fileName, iconName);
            addIconKey(m_iconNames, file.completeBaseName(), iconName);
            addIconKey(m_iconNames,
                       desktop.value(QStringLiteral("Desktop Entry/StartupWMClass")).toString(),
                       iconName);
        }
    }
}

QIcon IconResolver::iconForAppId(const QString &appId)
{
    if (m_cache.contains(appId))
        return m_cache.value(appId);

    QString iconName;
    const QFileInfo appInfo(appId);
    const QStringList candidates = {
        appId,
        appId + QStringLiteral(".desktop"),
        appInfo.fileName(),
        appInfo.completeBaseName(),
    };
    for (const QString &candidate : candidates) {
        if (m_iconNames.contains(candidate)) {
            iconName = m_iconNames.value(candidate);
            break;
        }
    }

    if (iconName.isEmpty())
        iconName = m_iconNames.value(appId);
    if (iconName.isEmpty())
        iconName = QString::fromLatin1(kFallbackIcon);

    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull() && iconName != QLatin1String(kFallbackIcon))
        icon = QIcon::fromTheme(QString::fromLatin1(kFallbackIcon));

    m_cache.insert(appId, icon);
    return icon;
}
