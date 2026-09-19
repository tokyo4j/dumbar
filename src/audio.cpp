#include "audio.h"

#include "dumbar.h"
#include "layershell/layershelltooltip.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QMetaObject>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <atomic>

#include <pulse/pulseaudio.h>

namespace
{
int volumePercent(const pa_cvolume &volume)
{
    const double normalized = static_cast<double>(pa_cvolume_avg(&volume))
                              / static_cast<double>(PA_VOLUME_NORM);
    return qBound(0, qRound(normalized * 100.0), 100);
}

QString endpointIcon(Audio::Endpoint endpoint, const Audio::VolumeState &state)
{
    if (endpoint == Audio::Endpoint::Microphone)
        return state.muted ? QStringLiteral("microphone-sensitivity-muted")
                           : QStringLiteral("microphone-sensitivity-high");
    if (state.muted)
        return QStringLiteral("audio-volume-muted");
    if (state.percent < 34)
        return QStringLiteral("audio-volume-low");
    if (state.percent < 67)
        return QStringLiteral("audio-volume-medium");
    return QStringLiteral("audio-volume-high");
}

QString endpointTooltip(Audio::Endpoint endpoint, const Audio::VolumeState &state)
{
    const QString name = endpoint == Audio::Endpoint::Microphone ? QStringLiteral("mic")
                                                                   : QStringLiteral("speaker");
    return QStringLiteral("%1: %2%").arg(name).arg(state.percent);
}

class AudioButton final : public QToolButton
{
public:
    AudioButton(Audio *audio, Audio::Endpoint endpoint, QWidget *parent)
        : QToolButton(parent)
        , m_audio(audio)
        , m_endpoint(endpoint)
    {
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setIconSize(QSize(DumbarStyle::kIconSize, DumbarStyle::kIconSize));
        setFixedWidth(DumbarStyle::kPanelButtonWidth);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            m_audio->changeVolume(m_endpoint, delta > 0 ? 5 : -5);
        event->accept();
    }

private:
    Audio *m_audio;
    Audio::Endpoint m_endpoint;
};
}

class AudioBackend final : public QObject
{
    Q_OBJECT

public:
    static AudioBackend *shared();

    ~AudioBackend() override;

    Audio::VolumeState state(Audio::Endpoint endpoint) const;
    void toggleMute(Audio::Endpoint endpoint);
    void setVolume(Audio::Endpoint endpoint, int percent);

Q_SIGNALS:
    void stateChanged();

private:
    explicit AudioBackend(QObject *parent);

    static void contextStateChanged(pa_context *context, void *data);
    static void subscriptionChanged(pa_context *context,
                                    pa_subscription_event_type_t type,
                                    uint32_t index,
                                    void *data);
    static void serverInfoReceived(pa_context *context,
                                   const pa_server_info *info,
                                   void *data);
    static void sinkInfoReceived(pa_context *context,
                                 const pa_sink_info *info,
                                 int endOfList,
                                 void *data);
    static void sourceInfoReceived(pa_context *context,
                                   const pa_source_info *info,
                                   int endOfList,
                                   void *data);

    void refreshDefaults();
    void refreshDefaultNodes(const pa_server_info *info);
    void refreshSink(const pa_sink_info *info);
    void refreshSource(const pa_source_info *info);
    void publishState();
    void reportUnavailable(const QString &reason);
    void setMuteOnPulseThread(Audio::Endpoint endpoint);
    void setVolumeOnPulseThread(Audio::Endpoint endpoint, int percent);

    static bool ready(pa_context *context);
    static void unref(pa_operation *operation);

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    bool m_mainloopStarted = false;
    std::atomic_bool m_stopping = false;

    QByteArray m_sinkName;
    QByteArray m_sourceName;
    Audio::VolumeState m_workerMicrophone;
    Audio::VolumeState m_workerSpeaker;
    int m_microphoneChannels = 2;
    int m_speakerChannels = 2;

    Audio::VolumeState m_microphone;
    Audio::VolumeState m_speaker;
};

AudioBackend *AudioBackend::shared()
{
    static AudioBackend *backend = new AudioBackend(QCoreApplication::instance());
    return backend;
}

AudioBackend::AudioBackend(QObject *parent)
    : QObject(parent)
{
    const QStringList iconNames {
        QStringLiteral("microphone-sensitivity-muted"),
        QStringLiteral("microphone-sensitivity-high"),
        QStringLiteral("audio-volume-muted"),
        QStringLiteral("audio-volume-low"),
        QStringLiteral("audio-volume-medium"),
        QStringLiteral("audio-volume-high"),
    };
    for (const QString &name : iconNames) {
        if (QIcon::fromTheme(name).isNull())
            qCWarning(lcDumbar) << "audio icon theme is missing icon"
                                 << "theme=" << QIcon::themeName()
                                 << "icon=" << name;
    }

    m_mainloop = pa_threaded_mainloop_new();
    if (!m_mainloop) {
        reportUnavailable(QStringLiteral("could not create a PulseAudio mainloop"));
        return;
    }

    pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_mainloop);
    m_context = pa_context_new(api, "dumbar");
    if (!m_context) {
        reportUnavailable(QStringLiteral("could not create a PulseAudio context"));
        return;
    }

    pa_context_set_state_callback(m_context, &AudioBackend::contextStateChanged, this);
    pa_context_set_subscribe_callback(m_context, &AudioBackend::subscriptionChanged, this);

    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        reportUnavailable(QStringLiteral("could not connect to PulseAudio: %1")
                              .arg(QString::fromUtf8(pa_strerror(pa_context_errno(m_context)))));
        return;
    }

    if (pa_threaded_mainloop_start(m_mainloop) < 0) {
        reportUnavailable(QStringLiteral("could not start the PulseAudio mainloop"));
    } else {
        m_mainloopStarted = true;
    }
}

AudioBackend::~AudioBackend()
{
    m_stopping.store(true);

    if (m_mainloop && m_mainloopStarted) {
        pa_threaded_mainloop_lock(m_mainloop);
        if (m_context)
            pa_context_disconnect(m_context);
        pa_threaded_mainloop_unlock(m_mainloop);
        pa_threaded_mainloop_stop(m_mainloop);
    }
    if (m_context)
        pa_context_unref(m_context);
    if (m_mainloop)
        pa_threaded_mainloop_free(m_mainloop);
}

Audio::VolumeState AudioBackend::state(Audio::Endpoint endpoint) const
{
    return endpoint == Audio::Endpoint::Microphone ? m_microphone : m_speaker;
}

void AudioBackend::toggleMute(Audio::Endpoint endpoint)
{
    if (!m_mainloop || !m_mainloopStarted || m_stopping.load())
        return;

    pa_threaded_mainloop_lock(m_mainloop);
    if (ready(m_context))
        setMuteOnPulseThread(endpoint);
    pa_threaded_mainloop_unlock(m_mainloop);
}

void AudioBackend::setVolume(Audio::Endpoint endpoint, int percent)
{
    if (!m_mainloop || !m_mainloopStarted || m_stopping.load())
        return;

    pa_threaded_mainloop_lock(m_mainloop);
    if (ready(m_context))
        setVolumeOnPulseThread(endpoint, qBound(0, percent, 100));
    pa_threaded_mainloop_unlock(m_mainloop);
}

bool AudioBackend::ready(pa_context *context)
{
    return context && pa_context_get_state(context) == PA_CONTEXT_READY;
}

void AudioBackend::unref(pa_operation *operation)
{
    if (operation)
        pa_operation_unref(operation);
}

void AudioBackend::contextStateChanged(pa_context *context, void *data)
{
    auto *backend = static_cast<AudioBackend *>(data);
    if (backend->m_stopping.load())
        return;

    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY: {
        const auto mask = static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK
                                                              | PA_SUBSCRIPTION_MASK_SOURCE
                                                              | PA_SUBSCRIPTION_MASK_SERVER);
        unref(pa_context_subscribe(context, mask, nullptr, nullptr));
        backend->refreshDefaults();
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        backend->reportUnavailable(QStringLiteral("PulseAudio connection failed: %1")
                                       .arg(QString::fromUtf8(pa_strerror(
                                           pa_context_errno(context)))));
        break;
    default:
        break;
    }
}

void AudioBackend::subscriptionChanged(pa_context *context,
                                       pa_subscription_event_type_t type,
                                       uint32_t,
                                       void *data)
{
    auto *backend = static_cast<AudioBackend *>(data);
    const pa_subscription_event_type_t facility =
        static_cast<pa_subscription_event_type_t>(type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
    if (!backend->m_stopping.load()
        && (facility == PA_SUBSCRIPTION_EVENT_SINK || facility == PA_SUBSCRIPTION_EVENT_SOURCE
            || facility == PA_SUBSCRIPTION_EVENT_SERVER))
        backend->refreshDefaults();
    Q_UNUSED(context);
}

void AudioBackend::refreshDefaults()
{
    if (!ready(m_context))
        return;
    unref(pa_context_get_server_info(m_context, &AudioBackend::serverInfoReceived, this));
}

void AudioBackend::serverInfoReceived(pa_context *, const pa_server_info *info, void *data)
{
    auto *backend = static_cast<AudioBackend *>(data);
    if (!info) {
        backend->reportUnavailable(QStringLiteral("could not query PulseAudio defaults"));
        return;
    }
    backend->refreshDefaultNodes(info);
}

void AudioBackend::refreshDefaultNodes(const pa_server_info *info)
{
    m_sinkName = info->default_sink_name ? QByteArray(info->default_sink_name) : QByteArray();
    m_sourceName = info->default_source_name ? QByteArray(info->default_source_name) : QByteArray();
    m_workerSpeaker = {};
    m_workerMicrophone = {};
    m_speakerChannels = 2;
    m_microphoneChannels = 2;
    publishState();

    if (!m_sinkName.isEmpty())
        unref(pa_context_get_sink_info_by_name(
            m_context, m_sinkName.constData(), &AudioBackend::sinkInfoReceived, this));
    if (!m_sourceName.isEmpty())
        unref(pa_context_get_source_info_by_name(
            m_context, m_sourceName.constData(), &AudioBackend::sourceInfoReceived, this));
}

void AudioBackend::sinkInfoReceived(pa_context *, const pa_sink_info *info, int endOfList, void *data)
{
    auto *backend = static_cast<AudioBackend *>(data);
    if (endOfList < 0) {
        backend->m_workerSpeaker = {};
        backend->publishState();
    } else if (info) {
        backend->refreshSink(info);
    }
}

void AudioBackend::sourceInfoReceived(pa_context *,
                                      const pa_source_info *info,
                                      int endOfList,
                                      void *data)
{
    auto *backend = static_cast<AudioBackend *>(data);
    if (endOfList < 0) {
        backend->m_workerMicrophone = {};
        backend->publishState();
    } else if (info) {
        backend->refreshSource(info);
    }
}

void AudioBackend::refreshSink(const pa_sink_info *info)
{
    if (m_sinkName.isEmpty() || !info->name || m_sinkName != info->name)
        return;
    m_speakerChannels = std::max(1, static_cast<int>(info->volume.channels));
    m_workerSpeaker.percent = volumePercent(info->volume);
    m_workerSpeaker.muted = info->mute != 0;
    m_workerSpeaker.valid = true;
    publishState();
}

void AudioBackend::refreshSource(const pa_source_info *info)
{
    if (m_sourceName.isEmpty() || !info->name || m_sourceName != info->name)
        return;
    m_microphoneChannels = std::max(1, static_cast<int>(info->volume.channels));
    m_workerMicrophone.percent = volumePercent(info->volume);
    m_workerMicrophone.muted = info->mute != 0;
    m_workerMicrophone.valid = true;
    publishState();
}

void AudioBackend::publishState()
{
    const Audio::VolumeState microphone = m_workerMicrophone;
    const Audio::VolumeState speaker = m_workerSpeaker;
    QMetaObject::invokeMethod(this,
                              [this, microphone, speaker] {
                                  if (m_stopping.load())
                                      return;
                                  m_microphone = microphone;
                                  m_speaker = speaker;
                                  emit stateChanged();
                              },
                              Qt::QueuedConnection);
}

void AudioBackend::reportUnavailable(const QString &reason)
{
    QMetaObject::invokeMethod(this,
                              [this, reason] {
                                  if (m_stopping.load())
                                      return;
                                  m_microphone = {};
                                  m_speaker = {};
                                  qWarning().noquote()
                                      << QStringLiteral("dumbar: PulseAudio backend unavailable: %1")
                                             .arg(reason);
                                  emit stateChanged();
                              },
                              Qt::QueuedConnection);
}

void AudioBackend::setMuteOnPulseThread(Audio::Endpoint endpoint)
{
    if (endpoint == Audio::Endpoint::Microphone) {
        if (m_sourceName.isEmpty())
            return;
        unref(pa_context_set_source_mute_by_name(
            m_context, m_sourceName.constData(), !m_workerMicrophone.muted, nullptr, nullptr));
    } else {
        if (m_sinkName.isEmpty())
            return;
        unref(pa_context_set_sink_mute_by_name(
            m_context, m_sinkName.constData(), !m_workerSpeaker.muted, nullptr, nullptr));
    }
}

void AudioBackend::setVolumeOnPulseThread(Audio::Endpoint endpoint, int percent)
{
    const bool microphone = endpoint == Audio::Endpoint::Microphone;
    const QByteArray &name = microphone ? m_sourceName : m_sinkName;
    if (name.isEmpty())
        return;

    const int channels = microphone ? m_microphoneChannels : m_speakerChannels;
    pa_cvolume volume;
    const pa_volume_t value = static_cast<pa_volume_t>(
        static_cast<double>(percent) * static_cast<double>(PA_VOLUME_NORM) / 100.0);
    pa_cvolume_set(&volume,
                   static_cast<unsigned>(std::max(channels, 1)),
                   value);
    if (microphone) {
        unref(pa_context_set_source_volume_by_name(
            m_context, name.constData(), &volume, nullptr, nullptr));
    } else {
        unref(pa_context_set_sink_volume_by_name(
            m_context, name.constData(), &volume, nullptr, nullptr));
    }
}

Audio::Audio(QWidget *parent)
    : QWidget(parent)
    , m_backend(AudioBackend::shared())
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_microphoneButton = new AudioButton(this, Endpoint::Microphone, this);
    m_speakerButton = new AudioButton(this, Endpoint::Speaker, this);
    m_microphoneTooltip = new LayerShellTooltip(this, m_microphoneButton);
    m_speakerTooltip = new LayerShellTooltip(this, m_speakerButton);
    layout->addWidget(m_microphoneButton);
    layout->addWidget(m_speakerButton);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    connect(m_microphoneButton, &QToolButton::clicked, this, [this] {
        toggleMute(Endpoint::Microphone);
    });
    connect(m_speakerButton, &QToolButton::clicked, this, [this] {
        toggleMute(Endpoint::Speaker);
    });
    connect(m_backend, &AudioBackend::stateChanged, this, &Audio::backendStateChanged);

    backendStateChanged();
}

void Audio::toggleMute(Endpoint endpoint)
{
    if (m_backend)
        m_backend->toggleMute(endpoint);
}

void Audio::changeVolume(Endpoint endpoint, int delta)
{
    if (!m_backend)
        return;

    const VolumeState &state = endpoint == Endpoint::Microphone ? m_microphone : m_speaker;
    const int current = state.valid ? state.percent : 50;
    m_backend->setVolume(endpoint, qBound(0, current + delta, 100));
}

void Audio::backendStateChanged()
{
    if (!m_backend)
        return;

    m_microphone = m_backend->state(Endpoint::Microphone);
    m_speaker = m_backend->state(Endpoint::Speaker);
    updateButtons();
    show();
}

void Audio::updateButtons()
{
    const auto update = [this](QToolButton *button,
                               Endpoint endpoint,
                               const VolumeState &state) {
        QIcon icon = QIcon::fromTheme(endpointIcon(endpoint, state));
        if (icon.isNull() && endpoint == Endpoint::Speaker) {
            icon = QApplication::style()->standardIcon(
                state.muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume);
        }
        button->setIcon(icon);
        LayerShellTooltip *tooltip = endpoint == Endpoint::Microphone ? m_microphoneTooltip
                                                                        : m_speakerTooltip;
        if (tooltip)
            tooltip->setText(endpointTooltip(endpoint, state));
    };

    update(m_microphoneButton, Endpoint::Microphone, m_microphone);
    update(m_speakerButton, Endpoint::Speaker, m_speaker);
}

#include "audio.moc"
