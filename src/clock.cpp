#include "clock.h"

#include "config.h"

#include <glibmm/main.h>

Clock::Clock()
{
    set_focus_on_click(false);
    set_child(m_label);

    m_calendar.set_show_week_numbers(true);
    m_popover.set_child(m_calendar);
    m_popover.set_has_arrow(false);
    m_popover.signal_show().connect([this] { m_calendar.select_day(Glib::DateTime::create_now_local()); });
    set_popover(m_popover);

    tick();
}

void Clock::tick()
{
    const Glib::DateTime now = Glib::DateTime::create_now_local();
    m_label.set_text(now.format(Config::kClockFormat));
    set_tooltip_text(now.format(Config::kClockTooltipFormat));

    const int elapsedInMinute = now.get_second() * 1000 + now.get_microsecond() / 1000;
    Glib::signal_timeout().connect_once(sigc::mem_fun(*this, &Clock::tick), 60000 - elapsedInMinute);
}
