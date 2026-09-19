#pragma once

#include <QHash>
#include <QIcon>
#include <QObject>

class IconResolver final : public QObject
{
    Q_OBJECT

public:
    explicit IconResolver(QObject *parent = nullptr);

    QIcon iconForAppId(const QString &appId);

private:
    void scanDesktopFiles();

    QHash<QString, QString> m_iconNames;
    QHash<QString, QIcon> m_cache;
};
