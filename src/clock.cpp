#include "clock.h"

#include "dumbar.h"

#include <QCalendarWidget>
#include <QDateTime>
#include <QScreen>
#include <QVariant>
#include <QWindow>

namespace
{
constexpr auto kClockFormat = "HH:mm";
}

Clock::Clock(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(this, &QToolButton::clicked, this, &Clock::showCalendar);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        updateClock();
        scheduleNextUpdate();
    });
    updateClock();
    scheduleNextUpdate();
}

void Clock::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    setText(now.toString(QString::fromLatin1(kClockFormat)));
    setToolTip(now.toString(QStringLiteral("dddd, MMMM d, yyyy")));
}

void Clock::showCalendar()
{
    if (m_calendar) {
        qCDebug(lcDumbar) << "closing calendar popup";
        m_calendar->close();
        return;
    }

    auto *calendar = new QCalendarWidget;
    calendar->setAttribute(Qt::WA_DeleteOnClose);
    calendar->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    calendar->setSelectedDate(QDate::currentDate());
    calendar->setFocusPolicy(Qt::StrongFocus);
    m_calendar = calendar;

    QWidget *panel = window();
    QScreen *panelScreen = panel ? panel->screen() : nullptr;
    qCDebug(lcDumbar) << "opening calendar popup"
                           << "screen=" << (panelScreen ? panelScreen->name() : QStringLiteral("<null>"))
                           << "screenGeometry=" << (panelScreen ? panelScreen->geometry() : QRect())
                           << "clockGeometryInPanel=" << (panel ? QRect(mapTo(panel, QPoint(0, 0)), size()) : QRect());

    // Qt's Wayland popup positioner uses coordinates relative to the
    // transient parent.  The panel is a layer-shell surface, so global
    // coordinates are not meaningful here (in particular when an output
    // starts below another output).
    calendar->winId();
    if (QWindow *calendarWindow = calendar->windowHandle()) {
        if (QWindow *panelWindow = panel ? panel->windowHandle() : nullptr) {
            calendarWindow->setTransientParent(panelWindow);

            const QPoint clockPosition = mapTo(panel, QPoint(0, 0));
            const QRect anchorRect(clockPosition, size());
            calendarWindow->setProperty("_q_waylandPopupAnchorRect", QVariant::fromValue(anchorRect));
            calendarWindow->setProperty("_q_waylandPopupAnchor",
                                        QVariant::fromValue(Qt::BottomEdge | Qt::RightEdge));
            calendarWindow->setProperty("_q_waylandPopupGravity",
                                        QVariant::fromValue(Qt::BottomEdge | Qt::LeftEdge));
            // xdg_positioner: slide_x | slide_y | flip_x | flip_y.
            calendarWindow->setProperty("_q_waylandPopupConstraintAdjustment",
                                        QVariant::fromValue(1u | 2u | 4u | 8u));
            qCDebug(lcDumbar) << "calendar popup configured"
                                   << "popupWindow=" << static_cast<void *>(calendarWindow)
                                   << "parentWindow=" << static_cast<void *>(panelWindow)
                                   << "anchorRect=" << anchorRect
                                   << "requestedSize=" << calendar->sizeHint();
        } else {
            qWarning("dumbar: could not find the panel window for the calendar popup");
        }
    } else {
        qWarning("dumbar: could not create the calendar's Wayland window");
    }

    connect(calendar, &QObject::destroyed, this, [this] {
        qCDebug(lcDumbar) << "calendar popup destroyed";
        m_calendar = nullptr;
    });
    calendar->show();
    calendar->setFocus();
    qCDebug(lcDumbar) << "calendar popup shown"
                           << "geometry=" << calendar->geometry();
}

void Clock::scheduleNextUpdate()
{
    const QDateTime now = QDateTime::currentDateTime();
    const int elapsedInMinute = now.time().second() * 1000 + now.time().msec();
    const int delay = qMax(100, 60000 - elapsedInMinute);
    m_timer.setSingleShot(true);
    m_timer.start(delay);
}
