#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QLabel;
class QEvent;
class QWidget;

class LayerShellTooltip final : public QObject
{
    Q_OBJECT

public:
    explicit LayerShellTooltip(QObject *parent, QWidget *anchor);

    void setText(const QString &text);
    void close();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void showWindow();

private:
    void show();
    void update();
    void hide();
    void closeWindow();

    QPointer<QWidget> m_anchor;
    QPointer<QLabel> m_window;
    QString m_text;
    QTimer m_timer;
};
