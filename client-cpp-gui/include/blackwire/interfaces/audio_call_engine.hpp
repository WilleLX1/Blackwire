#pragma once

#include <functional>
#include <vector>

#include <QByteArray>
#include <QString>

namespace blackwire {

struct AudioDeviceInfo {
    QString id;
    QString name;
};

class IAudioCallEngine {
public:
    using FrameHandler = std::function<void(const QByteArray&)>;

    virtual ~IAudioCallEngine() = default;

    virtual std::vector<AudioDeviceInfo> InputDevices() const = 0;
    virtual std::vector<AudioDeviceInfo> OutputDevices() const = 0;
    virtual QString DefaultInputDeviceId() const = 0;
    virtual QString DefaultOutputDeviceId() const = 0;

    virtual bool Start(
        const QString& input_device_id,
        const QString& output_device_id,
        FrameHandler on_frame,
        QString* warning,
        QString* error) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;

    virtual void SetMuted(bool muted) = 0;
    virtual bool ApplyDevices(const QString& input_device_id, const QString& output_device_id, QString* warning, QString* error) = 0;
    virtual void PushRemoteFrame(const QByteArray& frame) = 0;
};

}  // namespace blackwire
