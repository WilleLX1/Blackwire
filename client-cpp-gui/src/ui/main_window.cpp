#include "blackwire/ui/main_window.hpp"

#include <vector>

#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>

#include "blackwire/controller/application_controller.hpp"
#include "blackwire/ui/chat_widget.hpp"
#include "blackwire/ui/device_setup_widget.hpp"
#include "blackwire/ui/login_widget.hpp"
#include "blackwire/ui/settings_dialog.hpp"

namespace blackwire {

MainWindow::MainWindow(ApplicationController& controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
    setWindowTitle("Blackwire Client");
    resize(1100, 700);

    auto* stack = new QStackedWidget(this);
    stacked_container_ = stack;
    setCentralWidget(stack);

    login_widget_ = new LoginWidget(this);
    device_widget_ = new DeviceSetupWidget(this);
    chat_widget_ = new ChatWidget(this);
    settings_dialog_ = new SettingsDialog(this);

    login_widget_->SetBaseUrl(controller_.BaseUrl());
    chat_widget_->SetSettingsVisible(false);

    stack->addWidget(login_widget_);
    stack->addWidget(device_widget_);
    stack->addWidget(chat_widget_);
    stack->setCurrentWidget(login_widget_);

    connect(login_widget_, &LoginWidget::LoginRequested, this, [this]() {
        controller_.SetBaseUrl(login_widget_->BaseUrl());
        controller_.Login(login_widget_->Username(), login_widget_->Password());
    });

    connect(login_widget_, &LoginWidget::RegisterRequested, this, [this]() {
        controller_.SetBaseUrl(login_widget_->BaseUrl());
        controller_.Register(login_widget_->Username(), login_widget_->Password());
    });

    connect(device_widget_, &DeviceSetupWidget::DeviceSetupRequested, this, [this]() {
        controller_.SetupDevice(device_widget_->DeviceLabel());
    });

    connect(chat_widget_, &ChatWidget::NewConversationRequested, this, [this]() {
        controller_.OpenConversationByPeer(chat_widget_->PeerUsername());
    });

    connect(chat_widget_, &ChatWidget::ConversationSelected, this, [this](const QString& id) {
        controller_.SelectConversation(id);
    });

    connect(chat_widget_, &ChatWidget::SendMessageRequested, this, [this]() {
        controller_.SendMessageToPeer(chat_widget_->PeerUsername(), chat_widget_->ComposeText());
    });

    connect(chat_widget_, &ChatWidget::StartVoiceCallRequested, this, [this]() {
        controller_.StartVoiceCall();
    });

    connect(chat_widget_, &ChatWidget::AcceptVoiceCallRequested, this, [this]() {
        controller_.AcceptVoiceCall();
    });

    connect(chat_widget_, &ChatWidget::RejectVoiceCallRequested, this, [this]() {
        controller_.RejectVoiceCall();
    });

    connect(chat_widget_, &ChatWidget::EndVoiceCallRequested, this, [this]() {
        controller_.EndVoiceCall();
    });

    connect(chat_widget_, &ChatWidget::CallMuteToggled, this, [this](bool muted) {
        controller_.SetCallMuted(muted);
    });

    connect(chat_widget_, &ChatWidget::SettingsRequested, this, [this]() {
        controller_.LoadAudioDevices();
        controller_.LoadAccountDevices();
        settings_dialog_->SetIdentity(controller_.UserDisplayId());
        settings_dialog_->SetServerUrl(controller_.BaseUrl());
        settings_dialog_->SetDeviceInfo(controller_.DeviceLabel(), controller_.DeviceId());
        settings_dialog_->SetConnectionStatus(controller_.ConnectionStatus());
        settings_dialog_->SetDiagnostics(controller_.DiagnosticsReport());
        settings_dialog_->SetIntegrityWarning(last_integrity_warning_);
        settings_dialog_->SetAcceptMessagesFromStrangers(controller_.AcceptMessagesFromStrangers());
        settings_dialog_->exec();
    });

    connect(settings_dialog_, &SettingsDialog::CopyDiagnosticsRequested, this, [this]() {
        settings_dialog_->SetDiagnostics(controller_.DiagnosticsReport());
    });

    connect(settings_dialog_, &SettingsDialog::ApplyAudioDevicesRequested, this, [this](const QString& input, const QString& output) {
        controller_.SetPreferredAudioDevices(input, output);
    });

    connect(settings_dialog_, &SettingsDialog::AcceptMessagesFromStrangersChanged, this, [this](bool enabled) {
        controller_.SetAcceptMessagesFromStrangers(enabled);
    });

    connect(settings_dialog_, &SettingsDialog::RevokeDeviceRequested, this, [this](const QString& device_uid) {
        const auto answer = QMessageBox::question(
            this,
            "Kick Device",
            QString("Revoke device %1? It will be disconnected immediately.").arg(device_uid));
        if (answer != QMessageBox::Yes) {
            return;
        }
        controller_.RevokeDevice(device_uid);
    });

    connect(settings_dialog_, &SettingsDialog::LogoutRequested, this, [this]() {
        controller_.Logout();
    });

    connect(settings_dialog_, &SettingsDialog::ResetStateRequested, this, [this]() {
        controller_.ResetLocalState();
        login_widget_->SetBaseUrl(controller_.BaseUrl());
    });

    connect(
        &controller_,
        &ApplicationController::AuthStateChanged,
        this,
        [this, stack](bool authenticated, const QString&) {
            chat_widget_->SetIdentity(authenticated ? controller_.UserDisplayId() : QString());
            chat_widget_->SetSettingsVisible(authenticated);
            if (!authenticated) {
                last_integrity_warning_.clear();
                settings_dialog_->SetIntegrityWarning(last_integrity_warning_);
                settings_dialog_->SetAccountDevices(std::vector<DeviceOut>{}, QString());
                login_widget_->SetBaseUrl(controller_.BaseUrl());
                stack->setCurrentWidget(login_widget_);
                return;
            }
            stack->setCurrentWidget(device_widget_);
        });

    connect(&controller_, &ApplicationController::DeviceStateChanged, this, [this, stack](bool configured) {
        if (configured) {
            stack->setCurrentWidget(chat_widget_);
        }
    });

    connect(
        &controller_,
        &ApplicationController::ConversationListChanged,
        this,
        [this](const std::vector<ConversationListItemView>& items) {
            chat_widget_->SetConversationList(items);
        });

    connect(
        &controller_,
        &ApplicationController::ConversationSelected,
        this,
        [this](const QString& conversation_id, const std::vector<ThreadMessageView>& messages) {
            chat_widget_->SetSelectedConversation(conversation_id);
            chat_widget_->SetThreadMessages(messages);
        });

    connect(
        &controller_,
        &ApplicationController::IncomingMessage,
        this,
        [this](const QString& conversation_id, const ThreadMessageView& message) {
            if (conversation_id != chat_widget_->SelectedConversation()) {
                return;
            }
            chat_widget_->AppendThreadMessage(message);
        });

    connect(
        &controller_,
        &ApplicationController::MessageRequestReceived,
        this,
        [this](const QString& conversation_id, const QString& sender_username, const QString& preview_text) {
            QMessageBox message_box(this);
            message_box.setWindowTitle("Message Request");
            message_box.setIcon(QMessageBox::Question);

            const QString sender = sender_username.trimmed().isEmpty() ? "Unknown sender" : sender_username.trimmed();
            message_box.setText(QString("%1 sent you a message request.").arg(sender));

            QString preview = preview_text.trimmed();
            if (preview.isEmpty()) {
                preview = "(no preview)";
            }
            message_box.setInformativeText(QString("Preview: %1").arg(preview));

            auto* add_button = message_box.addButton("Add sender", QMessageBox::AcceptRole);
            message_box.addButton("Ignore", QMessageBox::RejectRole);
            message_box.setDefaultButton(add_button);
            message_box.exec();

            if (message_box.buttonRole(message_box.clickedButton()) == QMessageBox::RejectRole) {
                controller_.IgnoreMessageRequest(conversation_id);
                return;
            }
            controller_.AcceptMessageRequest(conversation_id);
        });

    connect(&controller_, &ApplicationController::MessageSendSucceeded, this, [this](const QString&, const QString&) {
        chat_widget_->ClearCompose();
    });

    connect(&controller_, &ApplicationController::ConnectionStatusChanged, this, [this](const QString& status) {
        chat_widget_->SetConnectionStatus(status);
        settings_dialog_->SetConnectionStatus(status);
        statusBar()->showMessage(status, 3000);
    });

    connect(&controller_, &ApplicationController::CallStateChanged, this, [this](const CallStateView& state) {
        chat_widget_->SetCallState(state);
    });

    connect(&controller_, &ApplicationController::IncomingCallReceived, this, [this](const CallStateView& state) {
        if (!state.conversation_id.trimmed().isEmpty()) {
            chat_widget_->SetSelectedConversation(state.conversation_id);
            controller_.SelectConversation(state.conversation_id);
        }
        chat_widget_->ShowBanner("Incoming voice call.", "info");
        statusBar()->showMessage("Incoming voice call", 4000);
    });

    connect(
        &controller_,
        &ApplicationController::AudioDevicesChanged,
        this,
        [this](const std::vector<AudioDeviceOptionView>& inputs, const std::vector<AudioDeviceOptionView>& outputs) {
            settings_dialog_->SetAudioDevices(inputs, outputs);
        });

    connect(
        &controller_,
        &ApplicationController::AccountDevicesChanged,
        this,
        [this](const std::vector<DeviceOut>& devices) {
            settings_dialog_->SetAccountDevices(devices, controller_.DeviceId());
        });

    connect(
        &controller_,
        &ApplicationController::AudioDevicePreferenceChanged,
        this,
        [this](const QString& input, const QString& output) {
            settings_dialog_->SetSelectedAudioDevices(input, output);
        });

    connect(&controller_, &ApplicationController::CallErrorOccurred, this, [this](const QString& error) {
        chat_widget_->ShowBanner(error, "warning");
        statusBar()->showMessage(error, 5000);
    });

    connect(&controller_, &ApplicationController::IntegrityWarningOccurred, this, [this](const QString& warning) {
        last_integrity_warning_ = warning.trimmed();
        settings_dialog_->SetIntegrityWarning(last_integrity_warning_);
        chat_widget_->ShowBanner(last_integrity_warning_, "warning");
        statusBar()->showMessage(last_integrity_warning_, 7000);
    });

    connect(&controller_, &ApplicationController::ErrorOccurred, this, [this, stack](const QString& error) {
        if (stack->currentWidget() == login_widget_ || stack->currentWidget() == device_widget_) {
            QMessageBox::warning(this, "Blackwire", error);
            return;
        }
        chat_widget_->ShowBanner(error, "error");
        statusBar()->showMessage(error, 5000);
    });
}

}  // namespace blackwire
