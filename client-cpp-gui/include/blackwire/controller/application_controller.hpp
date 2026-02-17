#pragma once

#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QObject>
#include <QMetaType>
#include <QString>

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

    void LoadConversations();
    void OpenConversationByPeer(const QString& username);
    void SelectConversation(const QString& conversation_id);
    void SendMessageToPeer(const QString& peer_username, const QString& message_text);
    void StartVoiceCall();
    void AcceptVoiceCall();
    void RejectVoiceCall();
    void EndVoiceCall();
    void SetCallMuted(bool muted);
    void LoadAudioDevices();
    void SetPreferredAudioDevices(const QString& input_device_id, const QString& output_device_id);
    bool AcceptMessagesFromStrangers() const;
    void SetAcceptMessagesFromStrangers(bool enabled);
    void AcceptMessageRequest(const QString& conversation_id);
    void IgnoreMessageRequest(const QString& conversation_id);
    void ResetLocalState();

    QString UserDisplayId() const;
    QString DeviceId() const;
    QString DeviceLabel() const;
    QString ConnectionStatus() const;
    QString DiagnosticsReport() const;
    QString ServerAuthority() const;

signals:
    void AuthStateChanged(bool authenticated, const QString& username);
    void DeviceStateChanged(bool configured);
    void ConversationListChanged(const std::vector<ConversationListItemView>& items);
    void ConversationSelected(const QString& conversation_id, const std::vector<ThreadMessageView>& thread_messages);
    void IncomingMessage(const QString& conversation_id, const ThreadMessageView& thread_message);
    void MessageRequestReceived(
        const QString& conversation_id,
        const QString& sender_username,
        const QString& preview_text);
    void MessageSendSucceeded(const QString& conversation_id, const QString& message_id);
    void ConnectionStatusChanged(const QString& status);
    void CallStateChanged(const CallStateView& state);
    void IncomingCallReceived(const CallStateView& state);
    void AudioDevicesChanged(
        const std::vector<AudioDeviceOptionView>& input_devices,
        const std::vector<AudioDeviceOptionView>& output_devices);
    void AudioDevicePreferenceChanged(const QString& input_device_id, const QString& output_device_id);
    void CallErrorOccurred(const QString& message);
    void ErrorOccurred(const QString& message);

private:
    std::string SecretNamespacePrefix() const;
    std::string SecretNamespaceNeedle() const;
    std::string SecretKey(const std::string& name) const;
    std::string RequireAccessToken();
    std::string RequireRefreshToken();
    void SaveTokenPair(const TokenBundle& tokens);
    void RefreshAccessToken();
    void StartRealtime();
    void StopRealtime();
    void PersistState();
    void RefreshConversationList();
    QString RenderMessage(const MessageOut& message, const std::string& plaintext) const;
    std::vector<ThreadMessageView> RenderThread(const std::string& conversation_id) const;
    std::optional<QString> NormalizePeerUsername(const QString& value, QString* error) const;
    bool IsWebSocketAuthError(const std::string& error) const;
    void ReauthenticateWebSocket();
    void RecordDiagnostic(const QString& line);
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
    DeviceOut ResolveRecipientDevice(const QString& normalized_peer);
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
    std::unordered_map<std::string, DeviceOut> peer_device_cache_;
    std::map<std::string, std::vector<LocalMessage>> pending_request_messages_;
    std::map<std::string, QString> pending_request_senders_;
    CallStateView call_state_;
    int audio_sequence_ = 0;
};

}  // namespace blackwire

Q_DECLARE_METATYPE(blackwire::ConversationListItemView)
Q_DECLARE_METATYPE(std::vector<blackwire::ConversationListItemView>)
Q_DECLARE_METATYPE(blackwire::AudioDeviceOptionView)
Q_DECLARE_METATYPE(std::vector<blackwire::AudioDeviceOptionView>)
Q_DECLARE_METATYPE(blackwire::CallStateView)
Q_DECLARE_METATYPE(blackwire::ThreadMessageView)
Q_DECLARE_METATYPE(std::vector<blackwire::ThreadMessageView>)
