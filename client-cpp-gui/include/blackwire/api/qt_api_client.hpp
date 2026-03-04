#pragma once

#include <QObject>
#include <QNetworkAccessManager>

#include "blackwire/interfaces/api_client.hpp"

namespace blackwire {

class QtApiClient final : public QObject, public IApiClient {
    Q_OBJECT

public:
    explicit QtApiClient(QObject* parent = nullptr);

    AuthResponse Register(
        const std::string& base_url,
        const std::string& username,
        const std::string& password) override;

    AuthResponse Login(
        const std::string& base_url,
        const std::string& username,
        const std::string& password) override;

    AuthResponse Refresh(
        const std::string& base_url,
        const std::string& refresh_token) override;

    void Logout(const std::string& base_url, const std::string& refresh_token) override;

    UserOut Me(const std::string& base_url, const std::string& access_token) override;

    AuthResponse RegisterDevice(
        const std::string& base_url,
        const std::string& bootstrap_token,
        const DeviceRegisterRequest& request) override;

    AuthResponse BindDevice(
        const std::string& base_url,
        const std::string& bootstrap_token,
        const std::string& device_uid,
        const std::string& nonce,
        long long timestamp_ms,
        const std::string& proof_signature_b64) override;

    std::vector<DeviceOut> ListDevices(
        const std::string& base_url,
        const std::string& access_token) override;

    DeviceOut RevokeDevice(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& device_uid) override;

    UserDeviceLookup GetUserDevice(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address) override;

    ResolvePrekeysResponse ResolvePrekeys(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address) override;

    PrekeyUploadResponse UploadPrekeys(
        const std::string& base_url,
        const std::string& access_token,
        const PrekeyUploadRequest& request) override;

    PresenceSetResponse SetPresenceStatus(
        const std::string& base_url,
        const std::string& access_token,
        const PresenceSetRequest& request) override;

    PresenceResolveResponse ResolvePresence(
        const std::string& base_url,
        const std::string& access_token,
        const PresenceResolveRequest& request) override;

    ConversationOut CreateDm(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address,
        const std::string& peer_username) override;

    ConversationOut CreateGroup(
        const std::string& base_url,
        const std::string& access_token,
        const CreateGroupConversationRequest& request) override;

    std::vector<ConversationOut> ListConversations(
        const std::string& base_url,
        const std::string& access_token) override;

    std::vector<ConversationMemberOut> ListConversationMembers(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id) override;

    std::vector<ConversationMemberOut> InviteConversationMembers(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id,
        const GroupInviteRequest& request) override;

    ConversationOut RenameConversationGroup(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id,
        const GroupRenameRequest& request) override;

    ConversationMemberOut AcceptConversationInvite(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id) override;

    ConversationMemberOut LeaveConversationGroup(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id) override;

    ConversationRecipientsOut GetConversationRecipients(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id) override;

    ConversationTypingResponse SendConversationTyping(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id,
        const ConversationTypingRequest& request) override;

    ConversationReadCursorOut SendConversationRead(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id,
        const ConversationReadRequest& request) override;

    ConversationReadStateOut GetConversationReadState(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id) override;

    SystemVersionOut GetSystemVersion(
        const std::string& base_url,
        const std::string& access_token) override;

    std::vector<MessageOut> ListMessages(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id,
        int limit,
        int offset) override;

    MessageSendResponse SendMessage(
        const std::string& base_url,
        const std::string& access_token,
        const MessageSendRequest& request) override;

private:
    nlohmann::json RequestJson(
        const QString& method,
        const QString& url,
        const QString& bearer_token,
        const nlohmann::json* body);

    QNetworkAccessManager network_;
};

}  // namespace blackwire
