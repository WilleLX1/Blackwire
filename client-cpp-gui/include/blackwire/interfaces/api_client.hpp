#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "blackwire/models/dto.hpp"

namespace blackwire {

class ApiException final : public std::runtime_error {
public:
    ApiException(int status_code, const std::string& message)
        : std::runtime_error(message), status_code_(status_code) {}

    int status_code() const noexcept { return status_code_; }

private:
    int status_code_;
};

class IApiClient {
public:
    virtual ~IApiClient() = default;

    virtual AuthResponse Register(
        const std::string& base_url,
        const std::string& username,
        const std::string& password) = 0;

    virtual AuthResponse Login(
        const std::string& base_url,
        const std::string& username,
        const std::string& password) = 0;

    virtual AuthResponse Refresh(
        const std::string& base_url,
        const std::string& refresh_token) = 0;

    virtual void Logout(const std::string& base_url, const std::string& refresh_token) = 0;

    virtual UserOut Me(const std::string& base_url, const std::string& access_token) = 0;

    virtual DeviceOut RegisterDevice(
        const std::string& base_url,
        const std::string& access_token,
        const DeviceRegisterRequest& request) = 0;

    virtual UserDeviceLookup GetUserDevice(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address) = 0;

    virtual ConversationOut CreateDm(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& peer_address,
        const std::string& peer_username) = 0;

    virtual std::vector<ConversationOut> ListConversations(
        const std::string& base_url,
        const std::string& access_token) = 0;

    virtual std::vector<MessageOut> ListMessages(
        const std::string& base_url,
        const std::string& access_token,
        const std::string& conversation_id,
        int limit,
        int offset) = 0;

    virtual MessageSendResponse SendMessage(
        const std::string& base_url,
        const std::string& access_token,
        const MessageSendRequest& request) = 0;
};

}  // namespace blackwire
