#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QLabel;
class QWidget;

class LayerShellTooltip final : public QObject
{
    Q_OBJECT

public:
    explicit LayerShellTooltip(QObject *parent = nullptr);

    void show(QWidget *anchor, const QString &text);
    void update(QWidget *anchor, const QString &text);
    void hide(QWidget *anchor);
    void close();

    bool isFor(QWidget *anchor) const;

private slots:
    void showWindow();

private:
    void closeWindow();

    QPointer<QWidget> m_anchor;
    QPointer<QLabel> m_window;
    QString m_text;
    QTimer m_timer;
};
