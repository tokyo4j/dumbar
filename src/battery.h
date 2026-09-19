#pragma once

#include <QDBusConnection>
#include <QWidget>

class QLabel;

class Battery final : public QWidget
{
    Q_OBJECT

public:
    explicit Battery(QWidget *parent = nullptr);

    static QString formatDuration(qint64 seconds);

private slots:
    void propertiesChanged(const QString &interfaceName,
                           const QVariantMap &changed,
                           const QStringList &invalidated);

private:
    void refresh();
    void applyProperties(const QVariantMap &properties);

    QDBusConnection m_bus;
    QLabel *m_icon = nullptr;
    QLabel *m_text = nullptr;
    bool m_warned = false;
};
