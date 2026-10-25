#pragma once

#include <gtkmm/calendar.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>

// Shows the time and toggles a calendar popover when clicked.
class Clock final : public Gtk::MenuButton
{
public:
    Clock();

private:
    // Updates the clock and schedules the next update at the start of the next minute.
    void tick();

    // A label child, unlike set_label(), comes without a dropdown arrow.
    Gtk::Label m_label;
    Gtk::Popover m_popover;
    Gtk::Calendar m_calendar;
};
