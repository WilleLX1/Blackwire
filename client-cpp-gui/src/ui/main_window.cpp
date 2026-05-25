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

namespace {

QString ValidateAuthFields(const QString& username, const QString& password, bool require_strong_password) {
    const QString normalized_username = username.trimmed();
    if (normalized_username.size() < 3) {
        return "Username must be at least 3 characters.";
    }
    if (password.size() < 8) {
        return "Password must be at least 8 characters.";
    }
    if (require_strong_password) {
        bool has_upper = false;
        bool has_lower = false;
        bool has_digit = false;
        bool has_special = false;
        for (const QChar ch : password) {
            has_upper = has_upper || ch.isUpper();
            has_lower = has_lower || ch.isLower();
            has_digit = has_digit || ch.isDigit();
            has_special = has_special || (!ch.isLetterOrNumber() && !ch.isSpace());
        }
        if (!has_upper || !has_lower || !has_digit || !has_special) {
            return "Password must include uppercase, lowercase, digit, and special character.";
        }
    }
    return QString();
}

}  // namespace

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
        const QString validation_error = ValidateAuthFields(
            login_widget_->Username(),
            login_widget_->Password(),
            false);
        if (!validation_error.isEmpty()) {
            QMessageBox::warning(this, "Blackwire", validation_error);
            return;
        }
        controller_.SetBaseUrl(login_widget_->BaseUrl());
        controller_.Login(login_widget_->Username(), login_widget_->Password());
    });

    connect(login_widget_, &LoginWidget::RegisterRequested, this, [this]() {
        const QString validation_error = ValidateAuthFields(
            login_widget_->Username(),
            login_widget_->Password(),
            true);
        if (!validation_error.isEmpty()) {
            QMessageBox::warning(this, "Blackwire", validation_error);
            return;
        }
        if (login_widget_->Password() != login_widget_->ConfirmPassword()) {
            QMessageBox::warning(this, "Blackwire", "Passwords do not match.");
            return;
        }
        controller_.SetBaseUrl(login_widget_->BaseUrl());
        controller_.Register(login_widget_->Username(), login_widget_->Password());
    });

    connect(
        login_widget_,
        &LoginWidget::RegistrationServerInfoRefreshRequested,
        this,
        [this](const QString& base_url) {
            try {
                login_widget_->SetRegistrationServerInfo(controller_.InspectRegistrationServer(base_url));
            } catch (const std::exception& ex) {
                login_widget_->SetRegistrationServerInfoError(ex.what());
            }
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

    connect(chat_widget_, &ChatWidget::CreateGroupFromDmRequested, this, [this]() {
        if (controller_.CreateGroupFromCurrentDm()) {
            chat_widget_->ShowBanner("Group created.", "info");
        }
    });

    connect(chat_widget_, &ChatWidget::GroupInviteDialogRequested, this, [this]() {
        if (!controller_.IsSelectedConversationOwnerManagedGroup()) {
            chat_widget_->ShowBanner("Only the group owner can invite members.", "warning");
            return;
        }
        const auto picker = controller_.LoadInvitableContactsForCurrentGroup(QString());
        chat_widget_->ShowGroupInviteDialog(picker);
    });

    connect(chat_widget_, &ChatWidget::InviteGroupMembersRequested, this, [this](const std::vector<QString>& addresses) {
        if (controller_.InviteContactsToCurrentGroup(addresses)) {
            chat_widget_->ShowBanner("Invites sent.", "info");
        }
    });

    connect(chat_widget_, &ChatWidget::GroupRenameRequested, this, [this](const QString& name) {
        if (controller_.RenameSelectedGroup(name)) {
            chat_widget_->ShowBanner("Group name updated.", "info");
        }
    });

    connect(
        chat_widget_,
        &ChatWidget::ConversationRemoveRequested,
        this,
        [this](const QString& conversation_id, const QString& conversation_type, bool can_manage_members, const QString& title) {
            const QString id = conversation_id.trimmed();
            if (id.isEmpty()) {
                return;
            }
            const QString normalized_type = conversation_type.trimmed().toLower();
            if (normalized_type == "group") {
                const QString question = can_manage_members
                                             ? "Leave this group? Ownership will be transferred automatically if needed."
                                             : "Leave this group?";
                const auto answer = QMessageBox::question(
                    this,
                    "Leave Group",
                    question);
                if (answer != QMessageBox::Yes) {
                    return;
                }
                if (controller_.LeaveGroupConversation(id)) {
                    chat_widget_->ShowBanner("Left group DM.", "info");
                }
                return;
            }

            const auto answer = QMessageBox::question(
                this,
                "Remove DM",
                QString("Remove %1 from Messages? This only clears local cache.").arg(title.trimmed().isEmpty() ? "this DM" : title));
            if (answer != QMessageBox::Yes) {
                return;
            }
            if (controller_.DismissDirectConversation(id)) {
                chat_widget_->ShowBanner("DM removed from Messages.", "info");
            }
        });

    connect(chat_widget_, &ChatWidget::SendMessageRequested, this, [this]() {
        controller_.SendMessageToPeer(QString(), chat_widget_->ComposeText());
    });

    connect(chat_widget_, &ChatWidget::SendFileRequested, this, [this](const QString& file_path) {
        controller_.SendFileToPeer(QString(), file_path);
    });

    connect(chat_widget_, &ChatWidget::RetryAttachmentRequested, this, [this](const QString& message_id) {
        controller_.RetryFailedAttachment(message_id);
    });

    connect(chat_widget_, &ChatWidget::TypingStateChanged, this, [this](const QString& conversation_id, bool typing) {
        controller_.PublishTypingState(conversation_id, typing);
    });

    connect(chat_widget_, &ChatWidget::UserStatusChanged, this, [this](const QString& status) {
        controller_.SetPresenceStatus(status);
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
        controller_.LoadSystemVersion();
        settings_dialog_->SetIdentity(controller_.UserDisplayId());
        settings_dialog_->SetServerUrl(controller_.BaseUrl());
        settings_dialog_->SetDeviceInfo(controller_.DeviceLabel(), controller_.DeviceId());
        settings_dialog_->SetVersionInfo(controller_.ClientVersion(), controller_.ServerVersion());
        settings_dialog_->SetConnectionStatus(controller_.ConnectionStatus());
        settings_dialog_->SetDiagnostics(controller_.DiagnosticsReport());
        settings_dialog_->SetIntegrityWarning(last_integrity_warning_);
        settings_dialog_->SetAcceptMessagesFromStrangers(controller_.AcceptMessagesFromStrangers());
        settings_dialog_->SetSaveMessageCache(controller_.SaveMessageCache());
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

    connect(settings_dialog_, &SettingsDialog::SaveMessageCacheChanged, this, [this](bool enabled) {
        controller_.SetSaveMessageCache(enabled);
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
        login_widget_->ShowLoginPage();
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
                settings_dialog_->SetVersionInfo(controller_.ClientVersion(), QString());
                login_widget_->SetBaseUrl(controller_.BaseUrl());
                login_widget_->ShowLoginPage();
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
            if (conversation_id.trimmed().isEmpty()) {
                chat_widget_->SetTypingIndicator(QString(), QString());
            }
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
        &ApplicationController::TypingIndicatorChanged,
        this,
        [this](const QString& conversation_id, const QString& text) {
            if (conversation_id != chat_widget_->SelectedConversation()) {
                return;
            }
            chat_widget_->SetTypingIndicator(conversation_id, text);
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

    connect(&controller_, &ApplicationController::UserPresenceChanged, this, [this](const QString& status) {
        chat_widget_->SetUserStatus(status);
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
