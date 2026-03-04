#include "blackwire/ui/settings_dialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaDevices>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtEndian>

#include "blackwire/models/dto.hpp"
#include "blackwire/models/view_models.hpp"

namespace blackwire {

namespace {

constexpr int kDesiredMonitorSampleRate = 16000;
constexpr int kDesiredMonitorChannels = 1;
constexpr int kAudioMeterMax = 100;
constexpr int kAudioMeterDecayMs = 45;
constexpr int kAudioMeterDecayStep = 5;
constexpr int kMonitorOutputQueueLimit = 96000;

std::string DeviceUid(const DeviceOut& device) {
    return device.device_uid.empty() ? device.id : device.device_uid;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Settings");
    setModal(true);
    resize(860, 540);

    BuildLayout();

    meter_decay_timer_.setInterval(kAudioMeterDecayMs);
    QObject::connect(&meter_decay_timer_, &QTimer::timeout, this, [this]() {
        if (mic_level_meter_ == nullptr) {
            return;
        }
        const int next = std::max(0, mic_level_meter_->value() - kAudioMeterDecayStep);
        mic_level_meter_->setValue(next);
    });

    QObject::connect(copy_id_button_, &QPushButton::clicked, this, [this]() {
        if (!identity_.isEmpty()) {
            QGuiApplication::clipboard()->setText(identity_);
        }
    });
    QObject::connect(copy_device_button_, &QPushButton::clicked, this, [this]() {
        if (!device_id_.isEmpty()) {
            QGuiApplication::clipboard()->setText(device_id_);
        }
    });
    QObject::connect(copy_diagnostics_button_, &QPushButton::clicked, this, [this]() {
        emit CopyDiagnosticsRequested();
        if (!diagnostics_.isEmpty()) {
            QGuiApplication::clipboard()->setText(diagnostics_);
        }
    });
    QObject::connect(account_devices_list_, &QListWidget::itemSelectionChanged, this, [this]() {
        const auto* item = account_devices_list_->currentItem();
        if (item == nullptr) {
            selected_account_device_uid_.clear();
            revoke_device_button_->setEnabled(false);
            return;
        }
        selected_account_device_uid_ = item->data(Qt::UserRole).toString();
        const QString status = item->data(Qt::UserRole + 1).toString().trimmed().toLower();
        const bool is_self = selected_account_device_uid_ == current_device_uid_;
        revoke_device_button_->setEnabled(
            !selected_account_device_uid_.isEmpty() && status == "active" && !is_self);
    });
    QObject::connect(revoke_device_button_, &QPushButton::clicked, this, [this]() {
        if (selected_account_device_uid_.isEmpty()) {
            return;
        }
        emit RevokeDeviceRequested(selected_account_device_uid_);
    });
    QObject::connect(apply_audio_button_, &QPushButton::clicked, this, [this]() {
        emit ApplyAudioDevicesRequested(
            input_device_combo_->currentData().toString(),
            output_device_combo_->currentData().toString());
    });
    QObject::connect(logout_button_, &QPushButton::clicked, this, [this]() {
        emit LogoutRequested();
        accept();
    });
    QObject::connect(reset_button_, &QPushButton::clicked, this, [this]() {
        emit ResetStateRequested();
        accept();
    });

    QObject::connect(
        input_device_combo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int) { RefreshAudioMonitor(); });
    QObject::connect(
        output_device_combo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int) { RefreshAudioMonitor(); });

    QObject::connect(mic_monitor_checkbox_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled) {
            StopAudioMonitor();
            mic_monitor_status_->setText("Monitor is off.");
            return;
        }

        QString error;
        if (!StartAudioMonitor(&error)) {
            const QSignalBlocker blocker(mic_monitor_checkbox_);
            mic_monitor_checkbox_->setChecked(false);
            mic_monitor_status_->setText(error.isEmpty() ? "Unable to start mic monitor." : error);
            return;
        }
    });

    QObject::connect(accept_messages_checkbox_, &QCheckBox::toggled, this, [this](bool enabled) {
        emit AcceptMessagesFromStrangersChanged(enabled);
    });

    QObject::connect(save_message_cache_checkbox_, &QCheckBox::toggled, this, [this](bool enabled) {
        emit SaveMessageCacheChanged(enabled);
    });

    QObject::connect(this, &QDialog::finished, this, [this](int) { StopAudioMonitor(); });
}

void SettingsDialog::BuildLayout() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("settingsSidebar");
    sidebar->setFixedWidth(240);

    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(14, 14, 14, 14);
    sidebar_layout->setSpacing(10);

    auto* sidebar_title = new QLabel("User Settings", sidebar);
    sidebar_title->setObjectName("dmSidebarTitle");
    sidebar_layout->addWidget(sidebar_title);

    tabs_list_ = new QListWidget(sidebar);
    tabs_list_->setObjectName("settingsTabList");
    tabs_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    tabs_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tabs_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabs_list_->setSpacing(4);
    tabs_list_->addItems({
        "My Account",
        "Voice & Audio",
        "Content & Social",
        "Data & Privacy",
        "Authorized Apps",
        "Session",
    });
    sidebar_layout->addWidget(tabs_list_, 1);

    auto* content = new QWidget(this);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(20, 18, 20, 18);
    content_layout->setSpacing(10);

    tabs_stack_ = new QStackedWidget(content);
    content_layout->addWidget(tabs_stack_, 1);

    root->addWidget(sidebar);
    root->addWidget(content, 1);

    BuildMyAccountPage();
    BuildVoiceAudioPage();
    BuildSocialPage();
    BuildPrivacyPage();
    BuildAppsPage();
    BuildSessionPage();

    QObject::connect(tabs_list_, &QListWidget::currentRowChanged, tabs_stack_, [this](int index) {
        if (index >= 0 && index < tabs_stack_->count()) {
            tabs_stack_->setCurrentIndex(index);
        }
    });

    tabs_list_->setCurrentRow(0);
}

void SettingsDialog::BuildMyAccountPage() {
    auto* page = new QWidget(tabs_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel("My Account", page);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    identity_value_ = new QLabel("-", page);
    server_url_value_ = new QLabel("-", page);
    device_label_value_ = new QLabel("-", page);
    device_id_value_ = new QLabel("-", page);
    client_version_value_ = new QLabel("-", page);
    server_version_value_ = new QLabel("-", page);
    connection_status_value_ = new QLabel("-", page);

    identity_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    server_url_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    device_label_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    device_id_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    client_version_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    server_version_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connection_status_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow("Your ID", identity_value_);
    form->addRow("Home Server URL", server_url_value_);
    form->addRow("Device Label", device_label_value_);
    form->addRow("Device ID", device_id_value_);
    form->addRow("Client Version", client_version_value_);
    form->addRow("Server Version", server_version_value_);
    form->addRow("Connection", connection_status_value_);
    layout->addLayout(form);

    auto* devices_title = new QLabel("Authorized Devices", page);
    devices_title->setObjectName("conversationSubtitle");
    layout->addWidget(devices_title);

    account_devices_list_ = new QListWidget(page);
    account_devices_list_->setObjectName("conversationList");
    account_devices_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    account_devices_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    account_devices_list_->setMinimumHeight(170);
    layout->addWidget(account_devices_list_);

    revoke_device_button_ = new QPushButton("Kick Selected Device", page);
    revoke_device_button_->setObjectName("dangerButton");
    revoke_device_button_->setEnabled(false);
    layout->addWidget(revoke_device_button_, 0, Qt::AlignLeft);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    copy_id_button_ = new QPushButton("Copy ID", page);
    copy_id_button_->setObjectName("secondaryButton");
    copy_device_button_ = new QPushButton("Copy Device ID", page);
    copy_device_button_->setObjectName("secondaryButton");
    actions->addWidget(copy_id_button_);
    actions->addWidget(copy_device_button_);
    actions->addStretch(1);
    layout->addLayout(actions);
    layout->addStretch(1);

    tabs_stack_->addWidget(page);
}

void SettingsDialog::BuildVoiceAudioPage() {
    auto* page = new QWidget(tabs_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel("Voice & Audio", page);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Choose your call devices and test your microphone in real-time.", page);
    subtitle->setObjectName("conversationSubtitle");
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    input_device_combo_ = new QComboBox(page);
    output_device_combo_ = new QComboBox(page);
    apply_audio_button_ = new QPushButton("Apply Audio Devices", page);
    apply_audio_button_->setObjectName("primaryButton");
    apply_audio_button_->setEnabled(false);

    form->addRow("Input Device", input_device_combo_);
    form->addRow("Output Device", output_device_combo_);
    form->addRow("", apply_audio_button_);
    layout->addLayout(form);

    mic_monitor_checkbox_ = new QCheckBox("Hear yourself (Mic Monitor)", page);
    layout->addWidget(mic_monitor_checkbox_);

    mic_level_meter_ = new QProgressBar(page);
    mic_level_meter_->setObjectName("audioLevelMeter");
    mic_level_meter_->setRange(0, kAudioMeterMax);
    mic_level_meter_->setValue(0);
    mic_level_meter_->setTextVisible(false);
    mic_level_meter_->setFixedHeight(10);
    layout->addWidget(mic_level_meter_);

    mic_monitor_status_ = new QLabel("Monitor is off.", page);
    mic_monitor_status_->setObjectName("connectionPill");
    layout->addWidget(mic_monitor_status_);
    layout->addStretch(1);

    tabs_stack_->addWidget(page);
}

void SettingsDialog::BuildSocialPage() {
    auto* page = new QWidget(tabs_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel("Content & Social", page);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* text = new QLabel("Choose whether strangers can send you new message requests.", page);
    text->setObjectName("conversationSubtitle");
    text->setWordWrap(true);
    layout->addWidget(text);

    accept_messages_checkbox_ = new QCheckBox("Accept messages from strangers", page);
    accept_messages_checkbox_->setChecked(true);
    layout->addWidget(accept_messages_checkbox_, 0, Qt::AlignLeft);

    layout->addStretch(1);

    tabs_stack_->addWidget(page);
}

void SettingsDialog::BuildPrivacyPage() {
    auto* page = new QWidget(tabs_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel("Data & Privacy", page);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* cache_section_title = new QLabel("Message Storage", page);
    cache_section_title->setObjectName("conversationSubtitle");
    layout->addWidget(cache_section_title);

    auto* cache_text =
        new QLabel("When enabled, messages are saved to an encrypted local cache so they persist across restarts. "
                    "When disabled, message history is cleared when the client exits.",
                    page);
    cache_text->setObjectName("conversationSubtitle");
    cache_text->setWordWrap(true);
    layout->addWidget(cache_text);

    save_message_cache_checkbox_ = new QCheckBox("Save messages to encrypted cache", page);
    save_message_cache_checkbox_->setChecked(false);
    layout->addWidget(save_message_cache_checkbox_, 0, Qt::AlignLeft);

    auto* separator = new QWidget(page);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background-color: #3f4147;");
    layout->addWidget(separator);

    auto* text =
        new QLabel("Diagnostics includes local state, connection status, and recent events for troubleshooting.", page);
    text->setObjectName("conversationSubtitle");
    text->setWordWrap(true);
    layout->addWidget(text);

    copy_diagnostics_button_ = new QPushButton("Copy Diagnostics", page);
    copy_diagnostics_button_->setObjectName("secondaryButton");
    layout->addWidget(copy_diagnostics_button_, 0, Qt::AlignLeft);

    auto* integrity_title = new QLabel("Integrity Status", page);
    integrity_title->setObjectName("conversationSubtitle");
    layout->addWidget(integrity_title);

    integrity_warning_value_ = new QLabel("No integrity warnings observed in this session.", page);
    integrity_warning_value_->setObjectName("connectionPill");
    integrity_warning_value_->setProperty("state", "connected");
    integrity_warning_value_->setWordWrap(true);
    layout->addWidget(integrity_warning_value_);

    layout->addStretch(1);

    tabs_stack_->addWidget(page);
}

void SettingsDialog::BuildAppsPage() {
    auto* page = new QWidget(tabs_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel("Authorized Apps", page);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* text = new QLabel("No authorized integrations for this profile.", page);
    text->setObjectName("conversationSubtitle");
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addStretch(1);

    tabs_stack_->addWidget(page);
}

void SettingsDialog::BuildSessionPage() {
    auto* page = new QWidget(tabs_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel("Session", page);
    title->setObjectName("threadTitle");
    layout->addWidget(title);

    auto* text = new QLabel("Use logout to clear auth tokens, or reset to remove all local profile state.", page);
    text->setObjectName("conversationSubtitle");
    text->setWordWrap(true);
    layout->addWidget(text);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    logout_button_ = new QPushButton("Logout", page);
    logout_button_->setObjectName("secondaryButton");
    reset_button_ = new QPushButton("Reset Local State", page);
    reset_button_->setObjectName("dangerButton");
    actions->addWidget(logout_button_);
    actions->addWidget(reset_button_);
    actions->addStretch(1);
    layout->addLayout(actions);
    layout->addStretch(1);

    tabs_stack_->addWidget(page);
}

void SettingsDialog::SetIdentity(const QString& identity) {
    identity_ = identity;
    identity_value_->setText(identity_.isEmpty() ? "-" : identity_);
    copy_id_button_->setEnabled(!identity_.isEmpty());
}

void SettingsDialog::SetServerUrl(const QString& server_url) {
    server_url_value_->setText(server_url.isEmpty() ? "-" : server_url);
}

void SettingsDialog::SetDeviceInfo(const QString& label, const QString& device_id) {
    device_id_ = device_id;
    device_label_value_->setText(label.isEmpty() ? "-" : label);
    device_id_value_->setText(device_id_.isEmpty() ? "-" : device_id_);
    copy_device_button_->setEnabled(!device_id_.isEmpty());
}

void SettingsDialog::SetVersionInfo(const QString& client_version, const QString& server_version) {
    if (client_version_value_ != nullptr) {
        client_version_value_->setText(client_version.trimmed().isEmpty() ? "-" : client_version.trimmed());
    }
    if (server_version_value_ != nullptr) {
        server_version_value_->setText(server_version.trimmed().isEmpty() ? "-" : server_version.trimmed());
    }
}

void SettingsDialog::SetConnectionStatus(const QString& status) {
    connection_status_value_->setText(status.isEmpty() ? "-" : status);
}

void SettingsDialog::SetDiagnostics(const QString& diagnostics) {
    diagnostics_ = diagnostics;
}

void SettingsDialog::SetAccountDevices(const std::vector<DeviceOut>& devices, const QString& current_device_uid) {
    if (account_devices_list_ == nullptr || revoke_device_button_ == nullptr) {
        return;
    }
    current_device_uid_ = current_device_uid.trimmed();
    selected_account_device_uid_.clear();
    revoke_device_button_->setEnabled(false);

    const QSignalBlocker blocker(account_devices_list_);
    account_devices_list_->clear();

    for (const auto& device : devices) {
        const QString uid = QString::fromStdString(DeviceUid(device));
        const QString label =
            QString::fromStdString(device.label).trimmed().isEmpty() ? "(unlabeled)" : QString::fromStdString(device.label).trimmed();
        const QString status = QString::fromStdString(device.status).trimmed().toLower().isEmpty()
                                   ? "active"
                                   : QString::fromStdString(device.status).trimmed().toLower();
        const QString suffix = uid == current_device_uid_ ? " (this device)" : "";

        auto* row = new QListWidgetItem(account_devices_list_);
        row->setData(Qt::UserRole, uid);
        row->setData(Qt::UserRole + 1, status);
        row->setText(QString("%1 [%2]%3\n%4").arg(label, status, suffix, uid));
        if (uid == current_device_uid_) {
            account_devices_list_->setCurrentItem(row);
        }
    }
}

void SettingsDialog::SetAudioDevices(
    const std::vector<AudioDeviceOptionView>& input_devices,
    const std::vector<AudioDeviceOptionView>& output_devices) {
    const QString selected_input = input_device_combo_->currentData().toString();
    const QString selected_output = output_device_combo_->currentData().toString();

    const QSignalBlocker input_blocker(input_device_combo_);
    const QSignalBlocker output_blocker(output_device_combo_);

    input_device_combo_->clear();
    for (const auto& device : input_devices) {
        input_device_combo_->addItem(device.name, device.id);
    }

    output_device_combo_->clear();
    for (const auto& device : output_devices) {
        output_device_combo_->addItem(device.name, device.id);
    }

    apply_audio_button_->setEnabled(input_device_combo_->count() > 0 && output_device_combo_->count() > 0);
    SetSelectedAudioDevices(selected_input, selected_output);
}

void SettingsDialog::SetSelectedAudioDevices(const QString& input_device_id, const QString& output_device_id) {
    const QSignalBlocker input_blocker(input_device_combo_);
    const QSignalBlocker output_blocker(output_device_combo_);

    const int input_index = input_device_combo_->findData(input_device_id);
    if (input_index >= 0) {
        input_device_combo_->setCurrentIndex(input_index);
    } else if (input_device_combo_->count() > 0) {
        input_device_combo_->setCurrentIndex(0);
    }

    const int output_index = output_device_combo_->findData(output_device_id);
    if (output_index >= 0) {
        output_device_combo_->setCurrentIndex(output_index);
    } else if (output_device_combo_->count() > 0) {
        output_device_combo_->setCurrentIndex(0);
    }

    RefreshAudioMonitor();
}

void SettingsDialog::SetAcceptMessagesFromStrangers(bool enabled) {
    if (accept_messages_checkbox_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(accept_messages_checkbox_);
    accept_messages_checkbox_->setChecked(enabled);
}

void SettingsDialog::SetIntegrityWarning(const QString& warning) {
    if (integrity_warning_value_ == nullptr) {
        return;
    }
    if (warning.trimmed().isEmpty()) {
        integrity_warning_value_->setText("No integrity warnings observed in this session.");
        integrity_warning_value_->setProperty("state", "connected");
    } else {
        integrity_warning_value_->setText(warning.trimmed());
        integrity_warning_value_->setProperty("state", "warning");
    }
    integrity_warning_value_->style()->unpolish(integrity_warning_value_);
    integrity_warning_value_->style()->polish(integrity_warning_value_);
}

bool SettingsDialog::AcceptMessagesFromStrangers() const {
    if (accept_messages_checkbox_ == nullptr) {
        return true;
    }
    return accept_messages_checkbox_->isChecked();
}

void SettingsDialog::SetSaveMessageCache(bool enabled) {
    if (save_message_cache_checkbox_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(save_message_cache_checkbox_);
    save_message_cache_checkbox_->setChecked(enabled);
}

bool SettingsDialog::SaveMessageCache() const {
    if (save_message_cache_checkbox_ == nullptr) {
        return false;
    }
    return save_message_cache_checkbox_->isChecked();
}

void SettingsDialog::RefreshAudioMonitor() {
    if (mic_monitor_checkbox_ == nullptr || !mic_monitor_checkbox_->isChecked()) {
        return;
    }

    QString error;
    if (!StartAudioMonitor(&error)) {
        const QSignalBlocker blocker(mic_monitor_checkbox_);
        mic_monitor_checkbox_->setChecked(false);
        mic_monitor_status_->setText(error.isEmpty() ? "Unable to start mic monitor." : error);
    }
}

bool SettingsDialog::StartAudioMonitor(QString* error) {
    StopAudioMonitor();

    if (input_device_combo_ == nullptr || output_device_combo_ == nullptr) {
        if (error != nullptr) {
            *error = "Audio monitor is unavailable.";
        }
        return false;
    }
    if (input_device_combo_->count() == 0 || output_device_combo_->count() == 0) {
        if (error != nullptr) {
            *error = "Select an input and output device first.";
        }
        return false;
    }

    const QString selected_input_id = input_device_combo_->currentData().toString();
    const QString selected_output_id = output_device_combo_->currentData().toString();

    bool input_fallback = false;
    bool output_fallback = false;

    QAudioDevice input_device;
    for (const auto& device : QMediaDevices::audioInputs()) {
        if (QString::fromUtf8(device.id()) == selected_input_id) {
            input_device = device;
            break;
        }
    }
    if (input_device.isNull()) {
        input_fallback = !selected_input_id.isEmpty();
        input_device = QMediaDevices::defaultAudioInput();
    }

    QAudioDevice output_device;
    for (const auto& device : QMediaDevices::audioOutputs()) {
        if (QString::fromUtf8(device.id()) == selected_output_id) {
            output_device = device;
            break;
        }
    }
    if (output_device.isNull()) {
        output_fallback = !selected_output_id.isEmpty();
        output_device = QMediaDevices::defaultAudioOutput();
    }

    if (input_device.isNull() || output_device.isNull()) {
        if (error != nullptr) {
            *error = "No usable audio devices found.";
        }
        return false;
    }

    QAudioFormat desired;
    desired.setSampleRate(kDesiredMonitorSampleRate);
    desired.setChannelCount(kDesiredMonitorChannels);
    desired.setSampleFormat(QAudioFormat::Int16);

    bool using_preferred_format = false;
    if (input_device.isFormatSupported(desired) && output_device.isFormatSupported(desired)) {
        monitor_format_ = desired;
    } else {
        using_preferred_format = true;
        monitor_format_ = output_device.preferredFormat();
        if (!input_device.isFormatSupported(monitor_format_)) {
            monitor_format_ = input_device.preferredFormat();
        }
        if (!output_device.isFormatSupported(monitor_format_)) {
            if (error != nullptr) {
                *error = "Selected devices do not support a shared monitor format.";
            }
            return false;
        }
    }

    monitor_source_ = new QAudioSource(input_device, monitor_format_, this);
    monitor_sink_ = new QAudioSink(output_device, monitor_format_, this);

    monitor_source_device_ = monitor_source_->start();
    if (monitor_source_device_ == nullptr) {
        StopAudioMonitor();
        if (error != nullptr) {
            *error = "Unable to start microphone capture.";
        }
        return false;
    }

    monitor_sink_device_ = monitor_sink_->start();
    if (monitor_sink_device_ == nullptr) {
        StopAudioMonitor();
        if (error != nullptr) {
            *error = "Unable to start speaker output.";
        }
        return false;
    }

    QObject::connect(monitor_source_device_, &QIODevice::readyRead, this, [this]() { DrainAudioMonitor(); });
    meter_decay_timer_.start();

    if (input_fallback || output_fallback) {
        mic_monitor_status_->setText("Monitoring with fallback to system default audio device.");
    } else if (using_preferred_format) {
        mic_monitor_status_->setText("Monitoring microphone (using device preferred format).");
    } else {
        mic_monitor_status_->setText("Monitoring microphone. Speak to verify input level.");
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void SettingsDialog::StopAudioMonitor() {
    meter_decay_timer_.stop();
    monitor_pending_output_.clear();
    monitor_source_device_ = nullptr;
    monitor_sink_device_ = nullptr;
    if (mic_level_meter_ != nullptr) {
        mic_level_meter_->setValue(0);
    }

    if (monitor_source_ != nullptr) {
        monitor_source_->stop();
        delete monitor_source_;
        monitor_source_ = nullptr;
    }
    if (monitor_sink_ != nullptr) {
        monitor_sink_->stop();
        delete monitor_sink_;
        monitor_sink_ = nullptr;
    }
}

void SettingsDialog::DrainAudioMonitor() {
    if (monitor_source_device_ == nullptr) {
        return;
    }

    const QByteArray chunk = monitor_source_device_->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    UpdateAudioLevel(chunk);
    if (monitor_sink_device_ == nullptr) {
        return;
    }

    monitor_pending_output_.append(chunk);
    if (monitor_pending_output_.size() > kMonitorOutputQueueLimit) {
        monitor_pending_output_.remove(0, monitor_pending_output_.size() - kMonitorOutputQueueLimit);
    }

    while (!monitor_pending_output_.isEmpty()) {
        if (monitor_sink_ != nullptr && monitor_sink_->bytesFree() <= 0) {
            break;
        }
        const qint64 written = monitor_sink_device_->write(monitor_pending_output_);
        if (written <= 0) {
            break;
        }
        monitor_pending_output_.remove(0, static_cast<int>(written));
    }
}

void SettingsDialog::UpdateAudioLevel(const QByteArray& chunk) {
    if (chunk.isEmpty() || mic_level_meter_ == nullptr) {
        return;
    }

    const qsizetype bytes_per_sample = monitor_format_.bytesPerSample();
    if (bytes_per_sample <= 0) {
        return;
    }

    const auto* raw = reinterpret_cast<const uchar*>(chunk.constData());
    const qsizetype raw_size = chunk.size();
    double peak = 0.0;

    if (monitor_format_.sampleFormat() == QAudioFormat::UInt8) {
        for (qsizetype i = 0; i < raw_size; i += bytes_per_sample) {
            const double centered = (static_cast<int>(raw[i]) - 128) / 128.0;
            peak = std::max(peak, std::abs(centered));
        }
    } else if (monitor_format_.sampleFormat() == QAudioFormat::Int16 && bytes_per_sample >= 2) {
        for (qsizetype i = 0; i + 1 < raw_size; i += bytes_per_sample) {
            const qint16 sample = static_cast<qint16>(qFromLittleEndian<quint16>(raw + i));
            peak = std::max(peak, std::abs(static_cast<double>(sample) / 32768.0));
        }
    } else if (monitor_format_.sampleFormat() == QAudioFormat::Int32 && bytes_per_sample >= 4) {
        for (qsizetype i = 0; i + 3 < raw_size; i += bytes_per_sample) {
            const qint32 sample = static_cast<qint32>(qFromLittleEndian<quint32>(raw + i));
            peak = std::max(peak, std::abs(static_cast<double>(sample) / 2147483648.0));
        }
    } else if (monitor_format_.sampleFormat() == QAudioFormat::Float && bytes_per_sample >= 4) {
        for (qsizetype i = 0; i + 3 < raw_size; i += bytes_per_sample) {
            float sample = 0.0f;
            std::memcpy(&sample, raw + i, sizeof(float));
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
        }
    } else {
        return;
    }

    peak = std::clamp(peak, 0.0, 1.0);
    const int level = static_cast<int>(std::lround(peak * kAudioMeterMax));
    mic_level_meter_->setValue(std::max(level, mic_level_meter_->value()));
}

void SettingsDialog::closeEvent(QCloseEvent* event) {
    StopAudioMonitor();
    QDialog::closeEvent(event);
}

}  // namespace blackwire
