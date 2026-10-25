#pragma once

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
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

class Audio final : public Gtk::Box
{
public:
    explicit Audio(AudioBackend &backend);

private:
    void setupButton(Gtk::Button &button, Gtk::Image &icon, AudioEndpoint endpoint);
    void update();

    AudioBackend &m_backend;
    Gtk::Button m_microphone;
    Gtk::Image m_microphoneIcon;
    Gtk::Button m_speaker;
    Gtk::Image m_speakerIcon;
};
