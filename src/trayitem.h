#pragma once

#include <QDBusConnection>
#include <QIcon>
#include <QObject>

class TrayItem final : public QObject
{
    Q_OBJECT

public:
    TrayItem(const QDBusConnection &bus,
             const QString &service,
             const QString &path,
             QObject *parent = nullptr);
    ~TrayItem() override;

    QString id() const { return m_service + m_path; }
    QString service() const { return m_service; }
    QString path() const { return m_path; }
    QString status() const { return m_status; }
    QString toolTip() const { return m_toolTip; }
    QIcon icon() const { return m_icon; }
    bool isValid() const { return m_valid; }
    bool isPassive() const { return m_status == QLatin1String("Passive"); }

    void activate(int x, int y);
    void secondaryActivate(int x, int y);
    void scroll(int delta, const QString &orientation);
    void contextMenu(int x, int y);

public slots:
    void refresh();

signals:
    void changed(TrayItem *item);
    void invalid(TrayItem *item);

private:
    void applyProperties(const QVariantMap &properties);

    QDBusConnection m_bus;
    QString m_service;
    QString m_path;
    QString m_status;
    QString m_toolTip;
    QIcon m_icon;
    bool m_itemIsMenu = false;
    QString m_menuPath;
    bool m_valid = false;
    bool m_missingIconWarningIssued = false;
};
