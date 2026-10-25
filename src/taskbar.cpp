#include "taskbar.h"

#include "config.h"
#include "taskbutton.h"
#include "toplevelmanager.h"
#include "workspacemanager.h"

#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>

#include <algorithm>
#include <cassert>

TaskBar::TaskBar(ToplevelManager &manager, WorkspaceManager &workspaces, IconResolver &icons, wl_output *output)
    : m_workspaces(workspaces)
    , m_icons(icons)
    , m_output(output)
{
    set_hexpand(true);
    set_overflow(Gtk::Overflow::HIDDEN);

    manager.toplevelUpdated.connect(sigc::mem_fun(*this, &TaskBar::updateToplevel));
    manager.toplevelRemoved.connect(sigc::mem_fun(*this, &TaskBar::removeToplevel));
    workspaces.changed.connect(sigc::mem_fun(*this, &TaskBar::updateWorkspaces));
    for (const auto &toplevel : manager.toplevels())
        updateToplevel(*toplevel);

    const auto target = Gtk::DropTarget::create(G_TYPE_STRING, Gdk::DragAction::MOVE);
    target->signal_enter().connect([this](double x, double) { return dragMotion(x); }, false);
    target->signal_motion().connect([this](double x, double) { return dragMotion(x); }, false);
    // The buttons are already in place when dropped.
    target->signal_drop().connect([this](const Glib::ValueBase &, double, double) { return m_dragged != nullptr; },
                                  false);
    add_controller(target);
}

TaskBar::~TaskBar()
{
    for (const auto &button : m_buttons)
        button->unparent();
}

Gtk::SizeRequestMode TaskBar::get_request_mode_vfunc() const
{
    return Gtk::SizeRequestMode::CONSTANT_SIZE;
}

void TaskBar::measure_vfunc(Gtk::Orientation orientation, int, int &minimum, int &natural, int &, int &) const
{
    // The panel gives the taskbar whatever the other widgets leave, so the
    // buttons must not contribute to the panel's width.
    minimum = 0;
    natural = 0;
    if (orientation == Gtk::Orientation::HORIZONTAL)
        return;

    for (const auto &button : m_buttons) {
        if (!button->get_visible())
            continue;
        int buttonMinimum, buttonNatural, ignored;
        button->measure(orientation, -1, buttonMinimum, buttonNatural, ignored, ignored);
        minimum = std::max(minimum, buttonMinimum);
        natural = std::max(natural, buttonNatural);
    }
}

void TaskBar::size_allocate_vfunc(int width, int height, int)
{
    const auto visibleCount = std::ranges::count_if(m_buttons, [](const auto &button) {
        return button->get_visible();
    });
    if (visibleCount == 0)
        return;

    const int preferredWidth = std::min<int>(Config::kTaskButtonPreferredWidth, width / visibleCount);
    int x = 0;
    for (const auto &button : m_buttons) {
        if (!button->get_visible())
            continue;
        // Buttons that do not fit are clipped rather than underallocated.
        int minimum, ignored;
        button->measure(Gtk::Orientation::HORIZONTAL, -1, minimum, ignored, ignored, ignored);
        const int buttonWidth = std::max(preferredWidth, minimum);
        button->size_allocate(Gtk::Allocation(x, 0, buttonWidth, height), -1);
        x += buttonWidth;
    }
}

bool TaskBar::shows(const Toplevel &toplevel) const
{
    return toplevel.onOutput(m_output) && toplevel.onActiveWorkspace();
}

void TaskBar::updateToplevel(Toplevel &toplevel)
{
    auto it = std::ranges::find(m_buttons, &toplevel, [](const auto &button) { return &button->toplevel(); });
    if (it == m_buttons.end()) {
        auto button = std::make_unique<TaskButton>(toplevel, m_workspaces, m_icons);
        button->set_parent(*this);
        addDragSource(*button);
        it = m_buttons.insert(m_buttons.end(), std::move(button));
    } else {
        (*it)->refresh();
    }
    (*it)->set_visible(shows(toplevel));
}

// The active workspace may have changed.
void TaskBar::updateWorkspaces()
{
    for (const auto &button : m_buttons)
        button->set_visible(shows(button->toplevel()));
}

void TaskBar::removeToplevel(Toplevel &toplevel)
{
    const auto it = std::ranges::find(m_buttons, &toplevel, [](const auto &button) { return &button->toplevel(); });
    assert(it != m_buttons.end());
    if (it->get() == m_dragged)
        m_dragged = nullptr;
    (*it)->unparent();
    m_buttons.erase(it);
}

void TaskBar::addDragSource(TaskButton &button)
{
    const auto source = Gtk::DragSource::create();
    source->set_actions(Gdk::DragAction::MOVE);
    source->signal_prepare().connect([this, &button](double, double) {
        m_dragged = &button;
        Glib::Value<Glib::ustring> value;
        value.init(value.value_type());
        return Gdk::ContentProvider::create(value);
    }, false);
    source->signal_drag_end().connect([this](const Glib::RefPtr<Gdk::Drag> &, bool) { m_dragged = nullptr; });
    button.add_controller(source);
}

Gdk::DragAction TaskBar::dragMotion(double x)
{
    // Only buttons of this taskbar can be dragged here.
    if (!m_dragged)
        return {};

    int right = 0;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        if (!m_buttons[i]->get_visible())
            continue;
        right += m_buttons[i]->get_width();
        if (x >= right)
            continue;
        if (m_buttons[i].get() != m_dragged) {
            const auto from = std::ranges::find(m_buttons, m_dragged, &std::unique_ptr<TaskButton>::get);
            std::unique_ptr<TaskButton> dragged = std::move(*from);
            m_buttons.erase(from);
            m_buttons.insert(m_buttons.begin() + i, std::move(dragged));
            queue_allocate();
        }
        break;
    }
    return Gdk::DragAction::MOVE;
}
