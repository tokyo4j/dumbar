#include "audio.h"
#include "config.h"
#include "iconresolver.h"
#include "panel.h"
#include "statusnotifierhost.h"
#include "toplevelmanager.h"

#include <gdk/wayland/gdkwayland.h>
#include <gtkmm/application.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/settings.h>

#include <memory>
#include <vector>

namespace
{
// Shares one set of services between the panels of all monitors.
class Dumbar final
{
public:
    explicit Dumbar(Gtk::Application &application);

private:
    // Keeps m_panels in sync with the display's list of monitors.
    void monitorsChanged(guint position, guint removed, guint added);

    Gtk::Application &m_application;
    Glib::RefPtr<Gio::ListModel> m_monitors;
    ToplevelManager m_toplevels;
    IconResolver m_icons;
    StatusNotifierHost m_statusNotifierHost;
    AudioBackend m_audioBackend;
    std::vector<std::unique_ptr<Panel>> m_panels;
};

GdkDisplay *defaultDisplay()
{
    return Gdk::Display::get_default()->gobj();
}

Dumbar::Dumbar(Gtk::Application &application)
    : m_application(application)
    , m_monitors(Gdk::Display::get_default()->get_monitors())
    , m_toplevels(gdk_wayland_display_get_wl_display(defaultDisplay()),
                  gdk_wayland_seat_get_wl_seat(gdk_display_get_default_seat(defaultDisplay())))
{
    m_monitors->signal_items_changed().connect(sigc::mem_fun(*this, &Dumbar::monitorsChanged));
    monitorsChanged(0, 0, m_monitors->get_n_items());
}

void Dumbar::monitorsChanged(guint position, guint removed, guint added)
{
    m_panels.erase(m_panels.begin() + position, m_panels.begin() + position + removed);
    for (guint i = position; i < position + added; ++i) {
        const auto monitor = std::dynamic_pointer_cast<Gdk::Monitor>(m_monitors->get_object(i));
        auto panel = std::make_unique<Panel>(monitor, m_toplevels, m_icons, m_statusNotifierHost, m_audioBackend);
        m_application.add_window(*panel);
        panel->present();
        m_panels.insert(m_panels.begin() + i, std::move(panel));
    }
}
}

int main(int argc, char **argv)
{
    gdk_set_allowed_backends("wayland");
    // The GPU renderers load the GPU driver stack (~90 MB of LLVM and Mesa) for
    // no benefit in a small panel.
    g_setenv("GSK_RENDERER", "cairo", false);
    const auto application = Gtk::Application::create("io.github.tokyo4j.dumbar",
                                                      Gio::Application::Flags::NON_UNIQUE);
    std::unique_ptr<Dumbar> dumbar;

    application->signal_startup().connect([] {
        Gtk::Settings::get_default()->property_gtk_icon_theme_name() = Config::kIconTheme;
        const auto css = Gtk::CssProvider::create();
        css->load_from_string(Config::kStyleSheet);
        Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(),
                                                     css,
                                                     GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    });
    application->signal_activate().connect([&] {
        // Keep running while no monitor is connected.
        application->hold();
        dumbar = std::make_unique<Dumbar>(*application);
    });
    application->signal_shutdown().connect([&] { dumbar.reset(); });

    return application->run(argc, argv);
}
