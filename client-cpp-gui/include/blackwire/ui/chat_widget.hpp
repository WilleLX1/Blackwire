#pragma once

#include <vector>

#include <QWidget>

#include "blackwire/models/view_models.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QStackedWidget;
class QTimer;
class QHBoxLayout;
class QWidget;

namespace blackwire {

class ChatWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ChatWidget(QWidget* parent = nullptr);

    QString PeerUsername() const;
    QString ComposeText() const;
    QString SelectedConversation() const;
    void SetSelectedConversation(const QString& conversation_id);

    void SetConversationList(const std::vector<ConversationListItemView>& conversations);
    void SetThreadMessages(const std::vector<ThreadMessageView>& messages);
    void AppendThreadMessage(const ThreadMessageView& message);
    void ShowGroupInviteDialog(const GroupInvitePickerView& picker);
    void SetConnectionStatus(const QString& status);
    void SetIdentity(const QString& user_address);
    void SetCallState(const CallStateView& state);
    void SetUserStatus(const QString& status);
    void SetTypingIndicator(const QString& conversation_id, const QString& text);
    void ShowBanner(const QString& text, const QString& severity);
    void ClearCompose();
    void SetSendEnabled(bool enabled);
    void SetSettingsVisible(bool visible);

signals:
    void NewConversationRequested();
    void ConversationSelected(const QString& conversation_id);
    void ConversationRemoveRequested(
        const QString& conversation_id,
        const QString& conversation_type,
        bool can_manage_members,
        const QString& title);
    void SendMessageRequested();
    void SendFileRequested(const QString& file_path);
    void RetryAttachmentRequested(const QString& message_id);
    void TypingStateChanged(const QString& conversation_id, bool typing);
    void SettingsRequested();
    void StartVoiceCallRequested();
    void AcceptVoiceCallRequested();
    void RejectVoiceCallRequested();
    void EndVoiceCallRequested();
    void CallMuteToggled(bool muted);
    void UserStatusChanged(const QString& status);
    void CreateGroupFromDmRequested();
    void GroupInviteDialogRequested();
    void InviteGroupMembersRequested(const std::vector<QString>& addresses);
    void GroupRenameRequested(const QString& name);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* CreateConversationItemWidget(const ConversationListItemView& item, bool selected, bool removable);
    QWidget* CreateThreadMessageWidget(const ThreadMessageView& message);
    bool IsTimelineNearBottom() const;
    void ScrollTimelineToBottom();
    void SetTimelineHasMessages(bool has_messages);
    void UpdateThreadHeader();
    void RefreshConversationSelectionStyles();
    void RefreshListSelectionStyles(QListWidget* list);
    void SyncContactSelection(const QString& conversation_id);
    void SetContactsMode(bool enabled);
    void UpdateContactsButtonState();
    void UpdatePresenceIndicator(const QString& status);
    void UpdateCallControls();
    QString SelectedConversationType() const;
    bool SelectedConversationCanManageMembers() const;

    QLineEdit* peer_input_;
    QPushButton* contacts_button_;
    QPushButton* new_chat_button_;
    QPushButton* settings_button_;
    QListWidget* conversations_list_;
    QListWidget* contacts_panel_list_;
    QListWidget* messages_list_;
    QStackedWidget* content_stack_;
    QWidget* chat_panel_;
    QWidget* contacts_panel_;
    QStackedWidget* timeline_stack_;
    QPlainTextEdit* compose_input_;
    QPushButton* attach_button_;
    QPushButton* send_button_;
    QComboBox* presence_combo_;
    QLabel* presence_indicator_;
    QLabel* status_label_;
    QLabel* call_status_label_;
    QWidget* call_panel_;
    QLabel* call_panel_avatar_;
    QLabel* call_panel_title_;
    QLabel* call_panel_subtitle_;
    QWidget* call_participants_panel_;
    QHBoxLayout* call_participants_layout_;
    QPushButton* call_button_;
    QPushButton* group_button_;
    QPushButton* invite_button_;
    QPushButton* accept_call_button_;
    QPushButton* decline_call_button_;
    QPushButton* mute_call_button_;
    QPushButton* end_call_button_;
    QLabel* identity_label_;
    QPushButton* copy_identity_button_;
    QLabel* banner_label_;
    QLabel* typing_indicator_label_;
    QLineEdit* thread_title_label_;
    QLabel* empty_state_label_;
    QTimer* banner_timer_;
    QTimer* typing_idle_timer_;
    QString identity_value_;
    CallStateView call_state_;
    bool contacts_mode_active_ = true;
    bool local_typing_active_ = false;
    QString local_typing_conversation_id_;
};

}  // namespace blackwire
