#pragma once

#include <vector>

#include <QWidget>

#include "blackwire/models/view_models.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
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
    void SetConnectionStatus(const QString& status);
    void SetIdentity(const QString& user_address);
    void SetCallState(const CallStateView& state);
    void ShowBanner(const QString& text, const QString& severity);
    void ClearCompose();
    void SetSendEnabled(bool enabled);
    void SetSettingsVisible(bool visible);

signals:
    void NewConversationRequested();
    void ConversationSelected(const QString& conversation_id);
    void SendMessageRequested();
    void SettingsRequested();
    void StartVoiceCallRequested();
    void AcceptVoiceCallRequested();
    void RejectVoiceCallRequested();
    void EndVoiceCallRequested();
    void CallMuteToggled(bool muted);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* CreateConversationItemWidget(const ConversationListItemView& item, bool selected) const;
    QWidget* CreateThreadMessageWidget(const ThreadMessageView& message) const;
    bool IsTimelineNearBottom() const;
    void ScrollTimelineToBottom();
    void SetTimelineHasMessages(bool has_messages);
    void UpdateThreadHeader();
    void RefreshConversationSelectionStyles();
    void UpdateCallControls();

    QLineEdit* peer_input_;
    QPushButton* new_chat_button_;
    QPushButton* settings_button_;
    QListWidget* conversations_list_;
    QListWidget* messages_list_;
    QStackedWidget* timeline_stack_;
    QPlainTextEdit* compose_input_;
    QPushButton* send_button_;
    QLabel* status_label_;
    QLabel* call_status_label_;
    QWidget* call_panel_;
    QLabel* call_panel_avatar_;
    QLabel* call_panel_title_;
    QLabel* call_panel_subtitle_;
    QPushButton* call_button_;
    QPushButton* accept_call_button_;
    QPushButton* decline_call_button_;
    QPushButton* mute_call_button_;
    QPushButton* end_call_button_;
    QLabel* identity_label_;
    QPushButton* copy_identity_button_;
    QLabel* banner_label_;
    QLabel* thread_title_label_;
    QLabel* empty_state_label_;
    QTimer* banner_timer_;
    QString identity_value_;
    CallStateView call_state_;
};

}  // namespace blackwire
