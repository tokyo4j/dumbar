#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QToolButton>

class LayerShellTooltip;

class TrayItem final : public QToolButton
{
    Q_OBJECT

public:
    TrayItem(const QDBusConnection &bus, QString service, QString path, QWidget *parent = nullptr);
    ~TrayItem() override;

    QString id() const { return m_service + m_path; }
    QString service() const { return m_service; }
    QString status() const { return m_status; }
    bool hasMenu() const { return !m_menuPath.isEmpty(); }
    bool isValid() const { return m_valid; }
    bool isPassive() const { return m_status == QLatin1String("Passive"); }

    QDBusMessage callMenu(const QString &method, const QVariantList &arguments = {}) const;
    void menuEvent(int itemId) const;

public slots:
    void refresh();

signals:
    void updated();
    void invalid();
    void menuRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void invoke(const QString &method, const QVariantList &arguments = {}) const;
    void applyProperties(const QVariantMap &properties);

    QDBusConnection m_bus;
    QString m_service;
    QString m_path;
    QString m_status;
    QString m_menuPath;
    bool m_itemIsMenu = false;
    bool m_valid = false;
    LayerShellTooltip *m_tooltip = nullptr;
};
