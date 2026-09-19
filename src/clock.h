#pragma once

#include <QPointer>
#include <QTimer>
#include <QToolButton>

class QCalendarWidget;
class LayerShellTooltip;

class Clock final : public QToolButton
{
    Q_OBJECT

public:
    explicit Clock(QWidget *parent = nullptr);

private slots:
    void updateClock();
    void showCalendar();

private:
    void scheduleNextUpdate();

    QTimer m_timer;
    QPointer<QCalendarWidget> m_calendar;
    LayerShellTooltip *m_tooltip = nullptr;
};
