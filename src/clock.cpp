#include "clock.h"

#include "dumbar.h"
#include "layershell/popup.h"
#include "layershell/layershelltooltip.h"

#include <QCalendarWidget>
#include <QDateTime>
#include <QEnterEvent>
#include <QScreen>

namespace
{
constexpr auto kClockFormat = "HH:mm";
}

Clock::Clock(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(this, &QToolButton::clicked, this, &Clock::showCalendar);
    m_tooltip = new LayerShellTooltip(this);
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
    const QString time = now.toString(QString::fromLatin1(kClockFormat));
    const QString date = now.toString(QStringLiteral("dddd, MMMM d, yyyy"));
    m_tooltipText = QStringLiteral("%1\n%2").arg(time, date);
    setText(time);
    m_tooltip->update(this, m_tooltipText);
}

void Clock::enterEvent(QEnterEvent *event)
{
    QToolButton::enterEvent(event);
    m_tooltip->show(this, m_tooltipText);
}

void Clock::leaveEvent(QEvent *event)
{
    m_tooltip->hide(this);
    QToolButton::leaveEvent(event);
}

void Clock::showCalendar()
{
    m_tooltip->close();

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

    if (!LayerShellPopup::configure(calendar, this, "calendar")) {
        qWarning("dumbar: could not configure the calendar popup");
    } else {
        qCDebug(lcDumbar) << "calendar popup configured"
                           << "requestedSize=" << calendar->sizeHint();
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
