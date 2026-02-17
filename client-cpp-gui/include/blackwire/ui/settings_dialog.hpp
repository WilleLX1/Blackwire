#pragma once

#include <vector>

#include <QByteArray>
#include <QAudioFormat>
#include <QDialog>
#include <QString>
#include <QTimer>

class QComboBox;
class QCheckBox;
class QCloseEvent;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QAudioSink;
class QAudioSource;
class QIODevice;
class QStackedWidget;

namespace blackwire {

struct AudioDeviceOptionView;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void SetIdentity(const QString& identity);
    void SetServerUrl(const QString& server_url);
    void SetDeviceInfo(const QString& label, const QString& device_id);
    void SetConnectionStatus(const QString& status);
    void SetDiagnostics(const QString& diagnostics);
    void SetAudioDevices(
        const std::vector<AudioDeviceOptionView>& input_devices,
        const std::vector<AudioDeviceOptionView>& output_devices);
    void SetSelectedAudioDevices(const QString& input_device_id, const QString& output_device_id);
    void SetAcceptMessagesFromStrangers(bool enabled);
    bool AcceptMessagesFromStrangers() const;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void LogoutRequested();
    void ResetStateRequested();
    void CopyDiagnosticsRequested();
    void ApplyAudioDevicesRequested(const QString& input_device_id, const QString& output_device_id);
    void AcceptMessagesFromStrangersChanged(bool enabled);

private:
    void BuildLayout();
    void BuildMyAccountPage();
    void BuildVoiceAudioPage();
    void BuildSocialPage();
    void BuildPrivacyPage();
    void BuildAppsPage();
    void BuildSessionPage();
    void RefreshAudioMonitor();
    bool StartAudioMonitor(QString* error);
    void StopAudioMonitor();
    void DrainAudioMonitor();
    void UpdateAudioLevel(const QByteArray& chunk);

    QString identity_;
    QString device_id_;
    QString diagnostics_;

    QListWidget* tabs_list_ = nullptr;
    QStackedWidget* tabs_stack_ = nullptr;

    QLabel* identity_value_ = nullptr;
    QLabel* server_url_value_ = nullptr;
    QLabel* device_label_value_ = nullptr;
    QLabel* device_id_value_ = nullptr;
    QLabel* connection_status_value_ = nullptr;
    QComboBox* input_device_combo_ = nullptr;
    QComboBox* output_device_combo_ = nullptr;
    QPushButton* apply_audio_button_ = nullptr;
    QCheckBox* accept_messages_checkbox_ = nullptr;
    QCheckBox* mic_monitor_checkbox_ = nullptr;
    QProgressBar* mic_level_meter_ = nullptr;
    QLabel* mic_monitor_status_ = nullptr;
    QPushButton* copy_id_button_ = nullptr;
    QPushButton* copy_device_button_ = nullptr;
    QPushButton* copy_diagnostics_button_ = nullptr;
    QPushButton* logout_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;

    QAudioSource* monitor_source_ = nullptr;
    QAudioSink* monitor_sink_ = nullptr;
    QIODevice* monitor_source_device_ = nullptr;
    QIODevice* monitor_sink_device_ = nullptr;
    QByteArray monitor_pending_output_;
    QAudioFormat monitor_format_;
    QTimer meter_decay_timer_;
};

}  // namespace blackwire
