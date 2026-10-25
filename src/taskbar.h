#pragma once

#include <gtkmm/widget.h>

#include <memory>
#include <vector>

class IconResolver;
class TaskButton;
class Toplevel;
class ToplevelManager;
struct wl_output;

// Lays out the task buttons of the windows on one output. Every button gets
// the same width, which shrinks once the preferred widths no longer fit.
// Buttons are reordered by dragging them.
class TaskBar final : public Gtk::Widget
{
public:
    TaskBar(ToplevelManager &manager, IconResolver &icons, wl_output *output);
    ~TaskBar() override;

protected:
    Gtk::SizeRequestMode get_request_mode_vfunc() const override;
    void measure_vfunc(Gtk::Orientation orientation,
                       int forSize,
                       int &minimum,
                       int &natural,
                       int &minimumBaseline,
                       int &naturalBaseline) const override;
    void size_allocate_vfunc(int width, int height, int baseline) override;

private:
    void updateToplevel(Toplevel &toplevel);
    void removeToplevel(Toplevel &toplevel);
    void addDragSource(TaskButton &button);
    // Moves the dragged button into the place of the button under the pointer.
    Gdk::DragAction dragMotion(double x);

    IconResolver &m_icons;
    wl_output *m_output;
    std::vector<std::unique_ptr<TaskButton>> m_buttons;
    TaskButton *m_dragged = nullptr;
};
