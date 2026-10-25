#pragma once

#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <sigc++/signal.h>

#include <pulse/context.h>
#include <pulse/glib-mainloop.h>
#include <pulse/volume.h>

enum class AudioEndpoint {
    Microphone,
    Speaker,
};

// Tracks the default PulseAudio sink and source.
class AudioBackend final
{
public:
    struct Device {
        pa_cvolume volume {};
        bool muted = false;

        double percent() const;
    };

    AudioBackend();
    ~AudioBackend();
    AudioBackend(const AudioBackend &) = delete;
    AudioBackend &operator=(const AudioBackend &) = delete;

    const Device &device(AudioEndpoint endpoint) const;
    void toggleMute(AudioEndpoint endpoint);
    void changeVolume(AudioEndpoint endpoint, double delta);

    sigc::signal<void()> changed;

private:
    void refresh();

    pa_glib_mainloop *m_mainloop;
    pa_context *m_context;
    Device m_speaker;
    Device m_microphone;
};

// Toggles mute when clicked and changes the volume when scrolled.
class VolumeButton final : public Gtk::Button
{
public:
    VolumeButton(AudioBackend &backend, AudioEndpoint endpoint);
    ~VolumeButton() override;

private:
    void update();

    AudioBackend &m_backend;
    AudioEndpoint m_endpoint;
    Gtk::Image m_icon;
    // GTK hides tooltips on every scroll event, so a tooltip would flicker
    // while the volume is scrolled. This popover stays open instead.
    Gtk::Popover m_tooltip;
    Gtk::Label m_tooltipLabel;
};
