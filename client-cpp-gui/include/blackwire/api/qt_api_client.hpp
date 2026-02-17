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

    DeviceOut RegisterDevice(
        const std::string& base_url,
        const std::string& access_token,
        const DeviceRegisterRequest& request) override;

    UserDeviceLookup GetUserDevice(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address) override;

    ConversationOut CreateDm(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address,
        const std::string& peer_username) override;

    std::vector<ConversationOut> ListConversations(
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
