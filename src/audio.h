#pragma once

#include <QWidget>

class QToolButton;
class AudioBackend;
class LayerShellTooltip;

class Audio final : public QWidget
{
    Q_OBJECT

public:
    enum class Endpoint {
        Microphone,
        Speaker,
    };

    struct VolumeState {
        double percent = 0.0;
        bool muted = false;
        bool valid = false;
    };

    explicit Audio(QWidget *parent = nullptr);

    void toggleMute(Endpoint endpoint);
    void changeVolume(Endpoint endpoint, double delta);

private slots:
    void backendStateChanged();

private:
    void updateButtons();

    AudioBackend *m_backend = nullptr;
    LayerShellTooltip *m_microphoneTooltip = nullptr;
    LayerShellTooltip *m_speakerTooltip = nullptr;
    VolumeState m_microphone;
    VolumeState m_speaker;
    QToolButton *m_microphoneButton = nullptr;
    QToolButton *m_speakerButton = nullptr;
};
