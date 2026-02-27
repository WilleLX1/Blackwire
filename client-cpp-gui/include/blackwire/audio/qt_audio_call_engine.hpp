#pragma once

#include <deque>
#include <memory>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QTimer>

#include "blackwire/interfaces/audio_call_engine.hpp"

class QAudioSink;
class QAudioSource;
class QIODevice;

namespace blackwire {

class QtAudioCallEngine final : public QObject, public IAudioCallEngine {
    Q_OBJECT

public:
    explicit QtAudioCallEngine(QObject* parent = nullptr);
    ~QtAudioCallEngine() override;

    std::vector<AudioDeviceInfo> InputDevices() const override;
    std::vector<AudioDeviceInfo> OutputDevices() const override;
    QString DefaultInputDeviceId() const override;
    QString DefaultOutputDeviceId() const override;

    bool Start(
        const QString& input_device_id,
        const QString& output_device_id,
        FrameHandler on_frame,
        QString* warning,
        QString* error) override;
    void Stop() override;
    bool IsRunning() const override;

    void SetMuted(bool muted) override;
    bool ApplyDevices(const QString& input_device_id, const QString& output_device_id, QString* warning, QString* error) override;
    void PushRemoteFrame(const QByteArray& frame) override;

private:
    QAudioDevice ResolveInputDevice(const QString& preferred_id, bool* fallback_used) const;
    QAudioDevice ResolveOutputDevice(const QString& preferred_id, bool* fallback_used) const;
    bool StartWithDevices(const QAudioDevice& input_device, const QAudioDevice& output_device, QString* error);
    void DrainCapture();
    void FlushPlayback();

    static constexpr int kSampleRate = 16000;
    static constexpr int kFrameBytes = 640;
    static constexpr int kSamplesPerFrame = kFrameBytes / 2;
    static constexpr int kPlaybackQueueLimit = 64;
    static constexpr int kPlaybackLatencyCapFrames = 24;
    static constexpr int kMaxFramesMixedPerTick = 8;

    QAudioFormat format_;
    std::unique_ptr<QAudioSource> source_;
    std::unique_ptr<QAudioSink> sink_;
    QIODevice* source_device_ = nullptr;
    QIODevice* sink_device_ = nullptr;
    FrameHandler on_frame_;
    QByteArray capture_buffer_;
    std::deque<QByteArray> playback_queue_;
    QTimer playback_timer_;
    bool muted_ = false;
    bool running_ = false;
    QString current_input_id_;
    QString current_output_id_;
};

}  // namespace blackwire
