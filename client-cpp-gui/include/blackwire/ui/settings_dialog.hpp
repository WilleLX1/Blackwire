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
struct DeviceOut;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void SetIdentity(const QString& identity);
    void SetServerUrl(const QString& server_url);
    void SetDeviceInfo(const QString& label, const QString& device_id);
    void SetVersionInfo(const QString& client_version, const QString& server_version);
    void SetConnectionStatus(const QString& status);
    void SetDiagnostics(const QString& diagnostics);
    void SetAccountDevices(const std::vector<DeviceOut>& devices, const QString& current_device_uid);
    void SetAudioDevices(
        const std::vector<AudioDeviceOptionView>& input_devices,
        const std::vector<AudioDeviceOptionView>& output_devices);
    void SetSelectedAudioDevices(const QString& input_device_id, const QString& output_device_id);
    void SetAcceptMessagesFromStrangers(bool enabled);
    void SetSaveMessageCache(bool enabled);
    void SetIntegrityWarning(const QString& warning);
    bool AcceptMessagesFromStrangers() const;
    bool SaveMessageCache() const;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void LogoutRequested();
    void ResetStateRequested();
    void CopyDiagnosticsRequested();
    void RevokeDeviceRequested(const QString& device_uid);
    void ApplyAudioDevicesRequested(const QString& input_device_id, const QString& output_device_id);
    void AcceptMessagesFromStrangersChanged(bool enabled);
    void SaveMessageCacheChanged(bool enabled);

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
    QString current_device_uid_;
    QString selected_account_device_uid_;
    QString diagnostics_;

    QListWidget* tabs_list_ = nullptr;
    QStackedWidget* tabs_stack_ = nullptr;

    QLabel* identity_value_ = nullptr;
    QLabel* server_url_value_ = nullptr;
    QLabel* device_label_value_ = nullptr;
    QLabel* device_id_value_ = nullptr;
    QLabel* client_version_value_ = nullptr;
    QLabel* server_version_value_ = nullptr;
    QLabel* connection_status_value_ = nullptr;
    QListWidget* account_devices_list_ = nullptr;
    QPushButton* revoke_device_button_ = nullptr;
    QComboBox* input_device_combo_ = nullptr;
    QComboBox* output_device_combo_ = nullptr;
    QPushButton* apply_audio_button_ = nullptr;
    QCheckBox* accept_messages_checkbox_ = nullptr;
    QCheckBox* save_message_cache_checkbox_ = nullptr;
    QCheckBox* mic_monitor_checkbox_ = nullptr;
    QProgressBar* mic_level_meter_ = nullptr;
    QLabel* mic_monitor_status_ = nullptr;
    QPushButton* copy_id_button_ = nullptr;
    QPushButton* copy_device_button_ = nullptr;
    QPushButton* copy_diagnostics_button_ = nullptr;
    QLabel* integrity_warning_value_ = nullptr;
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
