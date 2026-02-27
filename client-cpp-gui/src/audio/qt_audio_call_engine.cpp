#include "blackwire/audio/qt_audio_call_engine.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

namespace blackwire {

QtAudioCallEngine::QtAudioCallEngine(QObject* parent) : QObject(parent) {
    format_.setSampleRate(kSampleRate);
    format_.setChannelCount(1);
    format_.setSampleFormat(QAudioFormat::Int16);

    playback_timer_.setInterval(20);
    QObject::connect(&playback_timer_, &QTimer::timeout, this, [this]() {
        FlushPlayback();
    });
}

QtAudioCallEngine::~QtAudioCallEngine() = default;

std::vector<AudioDeviceInfo> QtAudioCallEngine::InputDevices() const {
    std::vector<AudioDeviceInfo> items;
    const auto devices = QMediaDevices::audioInputs();
    items.reserve(devices.size());
    for (const auto& device : devices) {
        items.push_back(AudioDeviceInfo{
            QString::fromUtf8(device.id()),
            device.description(),
        });
    }
    return items;
}

std::vector<AudioDeviceInfo> QtAudioCallEngine::OutputDevices() const {
    std::vector<AudioDeviceInfo> items;
    const auto devices = QMediaDevices::audioOutputs();
    items.reserve(devices.size());
    for (const auto& device : devices) {
        items.push_back(AudioDeviceInfo{
            QString::fromUtf8(device.id()),
            device.description(),
        });
    }
    return items;
}

QString QtAudioCallEngine::DefaultInputDeviceId() const {
    return QString::fromUtf8(QMediaDevices::defaultAudioInput().id());
}

QString QtAudioCallEngine::DefaultOutputDeviceId() const {
    return QString::fromUtf8(QMediaDevices::defaultAudioOutput().id());
}

QAudioDevice QtAudioCallEngine::ResolveInputDevice(const QString& preferred_id, bool* fallback_used) const {
    const auto devices = QMediaDevices::audioInputs();
    for (const auto& device : devices) {
        if (QString::fromUtf8(device.id()) == preferred_id) {
            if (fallback_used != nullptr) {
                *fallback_used = false;
            }
            return device;
        }
    }
    if (fallback_used != nullptr) {
        *fallback_used = !preferred_id.trimmed().isEmpty();
    }
    return QMediaDevices::defaultAudioInput();
}

QAudioDevice QtAudioCallEngine::ResolveOutputDevice(const QString& preferred_id, bool* fallback_used) const {
    const auto devices = QMediaDevices::audioOutputs();
    for (const auto& device : devices) {
        if (QString::fromUtf8(device.id()) == preferred_id) {
            if (fallback_used != nullptr) {
                *fallback_used = false;
            }
            return device;
        }
    }
    if (fallback_used != nullptr) {
        *fallback_used = !preferred_id.trimmed().isEmpty();
    }
    return QMediaDevices::defaultAudioOutput();
}

bool QtAudioCallEngine::Start(
    const QString& input_device_id,
    const QString& output_device_id,
    FrameHandler on_frame,
    QString* warning,
    QString* error) {
    Stop();

    bool input_fallback = false;
    bool output_fallback = false;
    const QAudioDevice input_device = ResolveInputDevice(input_device_id, &input_fallback);
    const QAudioDevice output_device = ResolveOutputDevice(output_device_id, &output_fallback);
    if (!StartWithDevices(input_device, output_device, error)) {
        return false;
    }

    current_input_id_ = QString::fromUtf8(input_device.id());
    current_output_id_ = QString::fromUtf8(output_device.id());
    on_frame_ = std::move(on_frame);
    muted_ = false;
    running_ = true;
    playback_timer_.start();

    if (warning != nullptr) {
        if (input_fallback || output_fallback) {
            *warning = "Configured audio device was unavailable. Default device selected.";
        } else {
            warning->clear();
        }
    }
    return true;
}

bool QtAudioCallEngine::StartWithDevices(const QAudioDevice& input_device, const QAudioDevice& output_device, QString* error) {
    if (input_device.isNull() || output_device.isNull()) {
        if (error != nullptr) {
            *error = "No usable audio input/output device found.";
        }
        return false;
    }

    source_ = std::make_unique<QAudioSource>(input_device, format_, this);
    sink_ = std::make_unique<QAudioSink>(output_device, format_, this);

    source_device_ = source_->start();
    if (source_device_ == nullptr) {
        if (error != nullptr) {
            *error = "Unable to start audio capture device.";
        }
        source_.reset();
        sink_.reset();
        return false;
    }

    sink_device_ = sink_->start();
    if (sink_device_ == nullptr) {
        if (error != nullptr) {
            *error = "Unable to start audio output device.";
        }
        source_->stop();
        source_.reset();
        sink_.reset();
        source_device_ = nullptr;
        return false;
    }

    QObject::connect(source_device_, &QIODevice::readyRead, this, [this]() {
        DrainCapture();
    });

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void QtAudioCallEngine::Stop() {
    playback_timer_.stop();
    capture_buffer_.clear();
    playback_queue_.clear();
    on_frame_ = nullptr;
    source_device_ = nullptr;
    sink_device_ = nullptr;

    if (source_ != nullptr) {
        source_->stop();
        source_.reset();
    }
    if (sink_ != nullptr) {
        sink_->stop();
        sink_.reset();
    }

    running_ = false;
}

bool QtAudioCallEngine::IsRunning() const {
    return running_;
}

void QtAudioCallEngine::SetMuted(bool muted) {
    muted_ = muted;
}

bool QtAudioCallEngine::ApplyDevices(const QString& input_device_id, const QString& output_device_id, QString* warning, QString* error) {
    current_input_id_ = input_device_id;
    current_output_id_ = output_device_id;

    if (!running_) {
        if (warning != nullptr) {
            warning->clear();
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    const auto frame_handler = on_frame_;
    const bool was_muted = muted_;
    if (!Start(input_device_id, output_device_id, frame_handler, warning, error)) {
        return false;
    }
    muted_ = was_muted;
    return true;
}

void QtAudioCallEngine::PushRemoteFrame(const QByteArray& frame) {
    if (!running_ || frame.isEmpty()) {
        return;
    }

    int offset = 0;
    while (offset < frame.size()) {
        const int remaining = static_cast<int>(frame.size()) - offset;
        const int chunk_size = std::min(kFrameBytes, remaining);
        playback_queue_.push_back(frame.mid(offset, chunk_size));
        offset += chunk_size;
    }

    while (static_cast<int>(playback_queue_.size()) > kPlaybackQueueLimit) {
        playback_queue_.pop_front();
    }
}

void QtAudioCallEngine::DrainCapture() {
    if (!running_ || source_device_ == nullptr) {
        return;
    }

    capture_buffer_.append(source_device_->readAll());
    while (capture_buffer_.size() >= kFrameBytes) {
        const QByteArray frame = capture_buffer_.left(kFrameBytes);
        capture_buffer_.remove(0, kFrameBytes);
        if (!muted_ && on_frame_) {
            on_frame_(frame);
        }
    }
}

void QtAudioCallEngine::FlushPlayback() {
    if (!running_ || sink_device_ == nullptr) {
        return;
    }

    QByteArray frame(kFrameBytes, '\0');
    if (playback_queue_.empty()) {
        // Keep writing silence when no remote audio is available.
    } else {
        // Mix multiple queued frames into a single 20ms buffer so group calls
        // do not build unbounded playback delay as participant count grows.
        const int frames_to_mix = std::min(kMaxFramesMixedPerTick, static_cast<int>(playback_queue_.size()));
        std::array<int, kSamplesPerFrame> mixed{};

        for (int i = 0; i < frames_to_mix; ++i) {
            QByteArray chunk = playback_queue_.front();
            playback_queue_.pop_front();
            if (chunk.size() < kFrameBytes) {
                chunk.append(QByteArray(kFrameBytes - chunk.size(), '\0'));
            } else if (chunk.size() > kFrameBytes) {
                chunk.truncate(kFrameBytes);
            }

            const auto* input = reinterpret_cast<const qint16*>(chunk.constData());
            for (int sample = 0; sample < kSamplesPerFrame; ++sample) {
                mixed[sample] += static_cast<int>(input[sample]);
            }
        }

        auto* output = reinterpret_cast<qint16*>(frame.data());
        const int divisor = std::max(1, frames_to_mix);
        for (int sample = 0; sample < kSamplesPerFrame; ++sample) {
            const int averaged = mixed[sample] / divisor;
            output[sample] = static_cast<qint16>(std::clamp(
                averaged,
                static_cast<int>(std::numeric_limits<qint16>::min()),
                static_cast<int>(std::numeric_limits<qint16>::max())));
        }

        while (static_cast<int>(playback_queue_.size()) > kPlaybackLatencyCapFrames) {
            playback_queue_.pop_front();
        }
    }

    const qint64 written = sink_device_->write(frame);
    if (written < 0 || written == frame.size()) {
        return;
    }

    const QByteArray remainder = frame.mid(static_cast<int>(written));
    playback_queue_.push_front(remainder);
}

}  // namespace blackwire
