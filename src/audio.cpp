#include "audio.h"

#include "config.h"

#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>

#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
constexpr auto kDefaultSink = "@DEFAULT_SINK@";
constexpr auto kDefaultSource = "@DEFAULT_SOURCE@";

void unref(pa_operation *operation)
{
    // Requests fail and return null while the context is not ready.
    if (operation)
        pa_operation_unref(operation);
}

const char *iconName(AudioEndpoint endpoint, const AudioBackend::Device &device)
{
    if (endpoint == AudioEndpoint::Microphone)
        return device.muted ? "microphone-sensitivity-muted" : "microphone-sensitivity-high";
    if (device.muted)
        return "audio-volume-muted";
    if (device.percent() < 34)
        return "audio-volume-low";
    if (device.percent() < 67)
        return "audio-volume-medium";
    return "audio-volume-high";
}
}

double AudioBackend::Device::percent() const
{
    if (!pa_cvolume_valid(&volume))
        return 0;
    return std::min(100.0, 100.0 * pa_cvolume_max(&volume) / PA_VOLUME_NORM);
}

AudioBackend::AudioBackend()
    : m_mainloop(pa_glib_mainloop_new(nullptr))
    , m_context(pa_context_new(pa_glib_mainloop_get_api(m_mainloop), "dumbar"))
{
    pa_context_set_state_callback(m_context, [](pa_context *context, void *data) {
        auto *backend = static_cast<AudioBackend *>(data);
        switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            unref(pa_context_subscribe(context,
                                       static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK
                                                                           | PA_SUBSCRIPTION_MASK_SOURCE
                                                                           | PA_SUBSCRIPTION_MASK_SERVER),
                                       nullptr,
                                       nullptr));
            backend->refresh();
            break;
        case PA_CONTEXT_FAILED:
            g_warning("PulseAudio connection failed: %s", pa_strerror(pa_context_errno(context)));
            backend->m_speaker = {};
            backend->m_microphone = {};
            backend->changed.emit();
            break;
        default:
            break;
        }
    }, this);
    pa_context_set_subscribe_callback(m_context, [](pa_context *, pa_subscription_event_type_t, uint32_t, void *data) {
        static_cast<AudioBackend *>(data)->refresh();
    }, this);

    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
        g_warning("Could not connect to PulseAudio: %s", pa_strerror(pa_context_errno(m_context)));
}

AudioBackend::~AudioBackend()
{
    pa_context_disconnect(m_context);
    pa_context_unref(m_context);
    pa_glib_mainloop_free(m_mainloop);
}

const AudioBackend::Device &AudioBackend::device(AudioEndpoint endpoint) const
{
    return endpoint == AudioEndpoint::Speaker ? m_speaker : m_microphone;
}

void AudioBackend::toggleMute(AudioEndpoint endpoint)
{
    if (endpoint == AudioEndpoint::Speaker)
        unref(pa_context_set_sink_mute_by_name(m_context, kDefaultSink, !m_speaker.muted, nullptr, nullptr));
    else
        unref(pa_context_set_source_mute_by_name(m_context, kDefaultSource, !m_microphone.muted, nullptr, nullptr));
}

void AudioBackend::changeVolume(AudioEndpoint endpoint, double delta)
{
    pa_cvolume volume = device(endpoint).volume;
    if (!pa_cvolume_valid(&volume))
        return;

    const double percent = std::clamp(device(endpoint).percent() + delta, 0.0, 100.0);
    pa_cvolume_scale(&volume, std::lround(percent / 100.0 * PA_VOLUME_NORM));
    if (endpoint == AudioEndpoint::Speaker)
        unref(pa_context_set_sink_volume_by_name(m_context, kDefaultSink, &volume, nullptr, nullptr));
    else
        unref(pa_context_set_source_volume_by_name(m_context, kDefaultSource, &volume, nullptr, nullptr));
}

void AudioBackend::refresh()
{
    unref(pa_context_get_sink_info_by_name(m_context, kDefaultSink,
        [](pa_context *, const pa_sink_info *info, int endOfList, void *data) {
            if (endOfList > 0)
                return;
            auto *backend = static_cast<AudioBackend *>(data);
            backend->m_speaker = info ? Device {info->volume, info->mute != 0} : Device {};
            backend->changed.emit();
        }, this));
    unref(pa_context_get_source_info_by_name(m_context, kDefaultSource,
        [](pa_context *, const pa_source_info *info, int endOfList, void *data) {
            if (endOfList > 0)
                return;
            auto *backend = static_cast<AudioBackend *>(data);
            backend->m_microphone = info ? Device {info->volume, info->mute != 0} : Device {};
            backend->changed.emit();
        }, this));
}

VolumeButton::VolumeButton(AudioBackend &backend, AudioEndpoint endpoint)
    : m_backend(backend)
    , m_endpoint(endpoint)
{
    m_icon.set_pixel_size(Config::kIconSize);
    set_child(m_icon);
    set_focus_on_click(false);
    signal_clicked().connect([this] { m_backend.toggleMute(m_endpoint); });

    const auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL | Gtk::EventControllerScroll::Flags::DISCRETE);
    scroll->signal_scroll().connect([this](double, double dy) {
        m_backend.changeVolume(m_endpoint, -dy * Config::kVolumeScrollStep);
        return true;
    }, false);
    add_controller(scroll);

    const auto motion = Gtk::EventControllerMotion::create();
    motion->signal_enter().connect([this](double, double) { m_tooltip.popup(); });
    motion->signal_leave().connect([this] { m_tooltip.popdown(); });
    add_controller(motion);

    m_tooltip.set_child(m_tooltipLabel);
    m_tooltip.set_autohide(false);
    m_tooltip.set_has_arrow(false);
    m_tooltip.set_parent(*this);

    m_backend.changed.connect(sigc::mem_fun(*this, &VolumeButton::update));
    update();
}

VolumeButton::~VolumeButton()
{
    m_tooltip.unparent();
}

void VolumeButton::update()
{
    const AudioBackend::Device &device = m_backend.device(m_endpoint);
    const char *name = m_endpoint == AudioEndpoint::Microphone ? "mic" : "speaker";
    m_icon.set_from_icon_name(iconName(m_endpoint, device));
    m_tooltipLabel.set_text(std::format("{}: {}%", name, std::lround(device.percent())));
}
