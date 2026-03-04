#pragma once

#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QObject>
#include <QMetaType>
#include <QString>

class QTimer;

#include "blackwire/interfaces/api_client.hpp"
#include "blackwire/interfaces/audio_call_engine.hpp"
#include "blackwire/interfaces/crypto_service.hpp"
#include "blackwire/interfaces/secret_store.hpp"
#include "blackwire/interfaces/ws_client.hpp"
#include "blackwire/models/view_models.hpp"
#include "blackwire/storage/client_state.hpp"
#include "blackwire/storage/state_store.hpp"

namespace blackwire {

class ApplicationController final : public QObject {
    Q_OBJECT

public:
    ApplicationController(
        IApiClient& api_client,
        IWsClient& ws_client,
        IAudioCallEngine& audio_engine,
        ICryptoService& crypto,
        ISecretStore& secret_store,
        StateStore& state_store,
        const QString& profile_name = "default",
        QObject* parent = nullptr);

    void Initialize();

    void SetBaseUrl(const QString& base_url);
    QString BaseUrl() const;

    void Register(const QString& username, const QString& password);
    void Login(const QString& username, const QString& password);
    void Logout();
    void SetupDevice(const QString& label);
    void LoadAccountDevices();
    void RevokeDevice(const QString& device_uid);

    void LoadConversations();
    void OpenConversationByPeer(const QString& username);
    void SelectConversation(const QString& conversation_id);
    bool DismissDirectConversation(const QString& conversation_id);
    bool LeaveGroupConversation(const QString& conversation_id);
    bool CreateGroupFromCurrentDm();
    GroupInvitePickerView LoadInvitableContactsForCurrentGroup(const QString& query);
    bool InviteContactsToCurrentGroup(const std::vector<QString>& peer_addresses);
    bool RenameSelectedGroup(const QString& new_name);
    bool IsSelectedConversationOwnerManagedGroup() const;
    void SendMessageToPeer(const QString& peer_username, const QString& message_text);
    void SendFileToPeer(const QString& peer_username, const QString& file_path);
    void StartVoiceCall();
    void AcceptVoiceCall();
    void RejectVoiceCall();
    void EndVoiceCall();
    void SetCallMuted(bool muted);
    void SetPresenceStatus(const QString& status);
    void LoadAudioDevices();
    void SetPreferredAudioDevices(const QString& input_device_id, const QString& output_device_id);
    bool AcceptMessagesFromStrangers() const;
    void SetAcceptMessagesFromStrangers(bool enabled);
    bool SaveMessageCache() const;
    void SetSaveMessageCache(bool enabled);
    void AcceptMessageRequest(const QString& conversation_id);
    void IgnoreMessageRequest(const QString& conversation_id);
    void PublishTypingState(const QString& conversation_id, bool typing);
    void RetryFailedAttachment(const QString& message_id);
    void LoadSystemVersion();
    void ResetLocalState();

    QString UserDisplayId() const;
    QString DeviceId() const;
    QString DeviceLabel() const;
    QString ConnectionStatus() const;
    QString DiagnosticsReport() const;
    QString ServerAuthority() const;
    QString ClientVersion() const;
    QString ServerVersion() const;

signals:
    void AuthStateChanged(bool authenticated, const QString& username);
    void DeviceStateChanged(bool configured);
    void ConversationListChanged(const std::vector<ConversationListItemView>& items);
    void ConversationSelected(const QString& conversation_id, const std::vector<ThreadMessageView>& thread_messages);
    void IncomingMessage(const QString& conversation_id, const ThreadMessageView& thread_message);
    void TypingIndicatorChanged(const QString& conversation_id, const QString& text);
    void MessageRequestReceived(
        const QString& conversation_id,
        const QString& sender_username,
        const QString& preview_text);
    void MessageSendSucceeded(const QString& conversation_id, const QString& message_id);
    void ConnectionStatusChanged(const QString& status);
    void UserPresenceChanged(const QString& status);
    void CallStateChanged(const CallStateView& state);
    void IncomingCallReceived(const CallStateView& state);
    void AudioDevicesChanged(
        const std::vector<AudioDeviceOptionView>& input_devices,
        const std::vector<AudioDeviceOptionView>& output_devices);
    void AccountDevicesChanged(const std::vector<DeviceOut>& devices);
    void AudioDevicePreferenceChanged(const QString& input_device_id, const QString& output_device_id);
    void CallErrorOccurred(const QString& message);
    void IntegrityWarningOccurred(const QString& message);
    void ErrorOccurred(const QString& message);

private:
    std::string SecretNamespacePrefix() const;
    std::string SecretNamespaceNeedle() const;
    std::string SecretKey(const std::string& name) const;
    std::string RequireBootstrapToken();
    std::string RequireAccessToken();
    std::string RequireRefreshToken();
    void SaveBootstrapToken(const TokenBundle& tokens);
    void SaveTokenPair(const TokenBundle& tokens);
    void RefreshAccessToken();
    void StartRealtime();
    void StopRealtime();
    void EncryptPlaintextCacheInState();
    void DecryptPlaintextCacheInState();
    void PersistState();
    void RefreshPresenceCache();
    QString NormalizePresenceStatus(const QString& status) const;
    QString PresenceForPeerAddress(const QString& peer_address) const;
    QString ResolvePeerAddressForConversation(const std::string& conversation_id) const;
    void AppendCallHistoryEntry(const QString& reason);
    void AppendGroupRenameHistoryEntry(
        const QString& conversation_id,
        const QString& actor_address,
        const QString& group_name,
        const QString& dedupe_suffix);
    QString FormatCallDuration(qint64 duration_ms) const;
    std::vector<CallParticipantView> BuildDirectCallParticipants(const QString& peer_user_id) const;
    void RefreshConversationList();
    QString RenderMessage(const MessageOut& message, const std::string& plaintext) const;
    std::vector<ThreadMessageView> RenderThread(const std::string& conversation_id) const;
    QString BuildTypingIndicatorText(const std::string& conversation_id) const;
    void PruneExpiredTypingIndicators();
    bool PublishReadCursorForConversation(const std::string& conversation_id);
    void MergeReadCursor(
        const std::string& conversation_id,
        const QString& reader_user_address,
        const QString& last_read_message_id,
        long long last_read_sent_at_ms,
        const QString& updated_at);
    std::optional<QString> NormalizePeerUsername(const QString& value, QString* error) const;
    bool IsWebSocketAuthError(const std::string& error) const;
    void ReauthenticateWebSocket();
    void RecordDiagnostic(const QString& line);
    void UploadCurrentDevicePrekeys();
    bool PreferRatchetV2b1() const;
    bool DeviceSupportsMessageMode(const DeviceOut& device, const std::string& mode) const;
    bool ConversationExists(const std::string& conversation_id) const;
    const ConversationOut* FindConversation(const std::string& conversation_id) const;
    void RevealConversation(const std::string& conversation_id, bool select_conversation);
    QString MessagePreview(const QString& message) const;
    QString FriendlyConversationTitle(const std::string& conversation_id) const;
    QString LastActivityForConversation(const std::string& conversation_id) const;
    QString PreferredInputDeviceId() const;
    QString PreferredOutputDeviceId() const;
    bool IsCallState(const QString& value) const;
    void EmitCallState();
    void TransitionCallState(
        const QString& state,
        const QString& call_id,
        const QString& conversation_id,
        const QString& peer_user_id,
        const QString& reason);
    bool StartAudioEngineForActiveCall(QString* warning, QString* error);
    void StopAudioEngine();
    void ReportCallError(const QString& message);
    std::optional<std::string> FindConversationIdForPeer(const QString& normalized_peer) const;
    std::string ResolveConversationIdForPeer(const QString& normalized_peer);
    std::vector<DeviceOut> ResolveRecipientDevices(const QString& normalized_peer);
    std::vector<DeviceOut> ResolveOwnActiveDevices();
    void UpsertConversationMeta(
        const std::string& conversation_id,
        const QString& peer_username,
        const QString& peer_address,
        const QString& preview,
        const QString& created_at);
    void ClearInMemoryState();

    IApiClient& api_client_;
    IWsClient& ws_client_;
    IAudioCallEngine& audio_engine_;
    ICryptoService& crypto_;
    ISecretStore& secret_store_;
    StateStore& state_store_;
    std::string profile_name_;

    ClientState state_;
    std::string selected_conversation_id_;
    bool ws_reauth_in_progress_ = false;
    QString connection_status_ = "Disconnected";
    std::deque<QString> diagnostics_;
    std::unordered_map<std::string, std::vector<DeviceOut>> peer_device_cache_;
    struct AttachmentPolicyCacheEntry {
        qint64 attachment_inline_max_bytes = 0;
        qint64 max_ciphertext_bytes = 0;
        std::string source = "local";
    };
    std::unordered_map<std::string, AttachmentPolicyCacheEntry> peer_attachment_policy_cache_;
    std::optional<AttachmentPolicyCacheEntry> local_attachment_policy_cache_;
    std::unordered_map<std::string, QString> peer_presence_status_by_address_;
    struct ReadCursorState {
        QString last_read_message_id;
        long long last_read_sent_at_ms = 0;
        QString updated_at;
    };
    std::unordered_map<std::string, std::unordered_map<std::string, ReadCursorState>> read_cursors_by_conversation_;
    std::unordered_map<std::string, long long> last_published_read_sent_at_by_conversation_;
    std::unordered_map<std::string, std::unordered_map<std::string, qint64>> typing_expiry_ms_by_conversation_;
    std::unordered_map<std::string, bool> local_typing_state_by_conversation_;
    QString server_version_ = "unknown";
    QString client_version_ = "0.1.0";
    QString user_presence_status_ = "active";
    std::map<std::string, std::vector<LocalMessage>> pending_request_messages_;
    std::map<std::string, QString> pending_request_senders_;
    CallStateView call_state_;
    bool call_initiated_locally_ = false;
    qint64 call_started_at_ms_ = 0;
    qint64 call_active_started_at_ms_ = 0;
    bool pending_outgoing_end_request_ = false;
    QTimer* presence_poll_timer_ = nullptr;
    QTimer* typing_expiry_timer_ = nullptr;
    int audio_sequence_ = 0;
};

}  // namespace blackwire

Q_DECLARE_METATYPE(blackwire::ConversationListItemView)
Q_DECLARE_METATYPE(std::vector<blackwire::ConversationListItemView>)
Q_DECLARE_METATYPE(blackwire::DeviceOut)
Q_DECLARE_METATYPE(std::vector<blackwire::DeviceOut>)
Q_DECLARE_METATYPE(blackwire::AudioDeviceOptionView)
Q_DECLARE_METATYPE(std::vector<blackwire::AudioDeviceOptionView>)
Q_DECLARE_METATYPE(blackwire::CallStateView)
Q_DECLARE_METATYPE(blackwire::ThreadMessageView)
Q_DECLARE_METATYPE(std::vector<blackwire::ThreadMessageView>)
