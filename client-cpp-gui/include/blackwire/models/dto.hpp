#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace blackwire {

struct UserOut {
    std::string id;
    std::string username;
    std::string created_at;
    std::string user_address;
    std::string home_server_onion;
};

struct TokenBundle {
    std::string access_token;
    std::string token_type;
    int access_expires_in = 0;
    std::string refresh_token;
    int refresh_expires_in = 0;
};

struct AuthResponse {
    UserOut user;
    TokenBundle tokens;
};

struct DeviceRegisterRequest {
    std::string label;
    std::string ik_ed25519_pub;
    std::string enc_x25519_pub;
};

struct DeviceOut {
    std::string id;
    std::string user_id;
    std::string label;
    std::string ik_ed25519_pub;
    std::string enc_x25519_pub;
    std::string created_at;
};

struct UserDeviceLookup {
    std::string username;
    std::string peer_address;
    DeviceOut device;
};

struct ConversationOut {
    std::string id;
    std::string kind = "local";
    std::string user_a_id;
    std::string user_b_id;
    std::string local_user_id;
    std::string created_at;
    std::string peer_username;
    std::string peer_server_onion;
    std::string peer_address;
};

struct CipherEnvelope {
    int version = 1;
    std::string alg = "libsodium-sealedbox-v1";
    std::string recipient_device_id;
    std::string ciphertext_b64;
    std::string aad_b64;
    std::string client_message_id;
};

struct MessageOut {
    std::string id;
    std::string conversation_id;
    std::string sender_user_id;
    std::string sender_address;
    std::string sender_device_id;
    std::string client_message_id;
    CipherEnvelope envelope;
    std::string created_at;
};

struct MessageSendRequest {
    std::string conversation_id;
    CipherEnvelope envelope;
};

struct MessageSendResponse {
    bool duplicate = false;
    MessageOut message;
};

struct WsEventMessageNew {
    MessageOut message;
};

struct VoiceCallOffer {
    std::string conversation_id;
};

struct VoiceCallAccept {
    std::string call_id;
};

struct VoiceCallReject {
    std::string call_id;
    std::string reason;
};

struct VoiceCallEnd {
    std::string call_id;
    std::string reason;
};

struct VoiceAudioChunk {
    std::string call_id;
    int sequence = 0;
    std::string pcm_b64;
};

struct WsEventCallIncoming {
    std::string call_id;
    std::string conversation_id;
    std::string from_user_id;
    std::string from_user_address;
};

struct WsEventCallRinging {
    std::string call_id;
    std::string conversation_id;
    std::string peer_user_id;
    std::string peer_user_address;
};

struct WsEventCallAccepted {
    std::string call_id;
    std::string conversation_id;
    std::string peer_user_id;
    std::string peer_user_address;
};

struct WsEventCallRejected {
    std::string call_id;
    std::string conversation_id;
    std::string reason;
};

struct WsEventCallBusy {
    std::string conversation_id;
    std::string reason;
};

struct WsEventCallEnded {
    std::string call_id;
    std::string reason;
    std::string by_user_id;
};

struct WsEventCallAudio {
    std::string call_id;
    std::string from_user_id;
    std::string from_user_address;
    int sequence = 0;
    std::string pcm_b64;
};

struct WsEventCallError {
    std::string code;
    std::string detail;
};

inline void to_json(nlohmann::json& j, const UserOut& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"username", v.username},
        {"created_at", v.created_at},
        {"user_address", v.user_address},
        {"home_server_onion", v.home_server_onion},
    };
}

inline void from_json(const nlohmann::json& j, UserOut& v) {
    j.at("id").get_to(v.id);
    j.at("username").get_to(v.username);
    j.at("created_at").get_to(v.created_at);
    v.user_address = j.value("user_address", "");
    v.home_server_onion = j.value("home_server_onion", "");
}

inline void to_json(nlohmann::json& j, const TokenBundle& v) {
    j = nlohmann::json{
        {"access_token", v.access_token},
        {"token_type", v.token_type},
        {"access_expires_in", v.access_expires_in},
        {"refresh_token", v.refresh_token},
        {"refresh_expires_in", v.refresh_expires_in},
    };
}

inline void from_json(const nlohmann::json& j, TokenBundle& v) {
    j.at("access_token").get_to(v.access_token);
    v.token_type = j.value("token_type", "bearer");
    j.at("access_expires_in").get_to(v.access_expires_in);
    j.at("refresh_token").get_to(v.refresh_token);
    j.at("refresh_expires_in").get_to(v.refresh_expires_in);
}

inline void to_json(nlohmann::json& j, const AuthResponse& v) {
    j = nlohmann::json{{"user", v.user}, {"tokens", v.tokens}};
}

inline void from_json(const nlohmann::json& j, AuthResponse& v) {
    j.at("user").get_to(v.user);
    j.at("tokens").get_to(v.tokens);
}

inline void to_json(nlohmann::json& j, const DeviceRegisterRequest& v) {
    j = nlohmann::json{
        {"label", v.label},
        {"ik_ed25519_pub", v.ik_ed25519_pub},
        {"enc_x25519_pub", v.enc_x25519_pub},
    };
}

inline void from_json(const nlohmann::json& j, DeviceRegisterRequest& v) {
    j.at("label").get_to(v.label);
    j.at("ik_ed25519_pub").get_to(v.ik_ed25519_pub);
    j.at("enc_x25519_pub").get_to(v.enc_x25519_pub);
}

inline void to_json(nlohmann::json& j, const DeviceOut& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"user_id", v.user_id},
        {"label", v.label},
        {"ik_ed25519_pub", v.ik_ed25519_pub},
        {"enc_x25519_pub", v.enc_x25519_pub},
        {"created_at", v.created_at},
    };
}

inline void from_json(const nlohmann::json& j, DeviceOut& v) {
    j.at("id").get_to(v.id);
    j.at("user_id").get_to(v.user_id);
    j.at("label").get_to(v.label);
    j.at("ik_ed25519_pub").get_to(v.ik_ed25519_pub);
    j.at("enc_x25519_pub").get_to(v.enc_x25519_pub);
    j.at("created_at").get_to(v.created_at);
}

inline void to_json(nlohmann::json& j, const UserDeviceLookup& v) {
    j = nlohmann::json{{"username", v.username}, {"peer_address", v.peer_address}, {"device", v.device}};
}

inline void from_json(const nlohmann::json& j, UserDeviceLookup& v) {
    j.at("username").get_to(v.username);
    v.peer_address = j.value("peer_address", "");
    j.at("device").get_to(v.device);
}

inline void to_json(nlohmann::json& j, const ConversationOut& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"kind", v.kind},
        {"user_a_id", v.user_a_id},
        {"user_b_id", v.user_b_id},
        {"local_user_id", v.local_user_id},
        {"created_at", v.created_at},
        {"peer_username", v.peer_username},
        {"peer_server_onion", v.peer_server_onion},
        {"peer_address", v.peer_address},
    };
}

inline void from_json(const nlohmann::json& j, ConversationOut& v) {
    j.at("id").get_to(v.id);
    v.kind = j.value("kind", "local");
    v.user_a_id = j.value("user_a_id", "");
    v.user_b_id = j.value("user_b_id", "");
    v.local_user_id = j.value("local_user_id", "");
    j.at("created_at").get_to(v.created_at);
    v.peer_username = j.value("peer_username", "");
    v.peer_server_onion = j.value("peer_server_onion", "");
    v.peer_address = j.value("peer_address", "");
}

inline void to_json(nlohmann::json& j, const CipherEnvelope& v) {
    j = nlohmann::json{
        {"version", v.version},
        {"alg", v.alg},
        {"recipient_device_id", v.recipient_device_id},
        {"ciphertext_b64", v.ciphertext_b64},
        {"aad_b64", v.aad_b64.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.aad_b64)},
        {"client_message_id", v.client_message_id},
    };
}

inline void from_json(const nlohmann::json& j, CipherEnvelope& v) {
    v.version = j.value("version", 1);
    v.alg = j.value("alg", "libsodium-sealedbox-v1");
    j.at("recipient_device_id").get_to(v.recipient_device_id);
    j.at("ciphertext_b64").get_to(v.ciphertext_b64);
    if (j.contains("aad_b64") && !j.at("aad_b64").is_null()) {
        j.at("aad_b64").get_to(v.aad_b64);
    } else {
        v.aad_b64.clear();
    }
    j.at("client_message_id").get_to(v.client_message_id);
}

inline void to_json(nlohmann::json& j, const MessageOut& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"conversation_id", v.conversation_id},
        {"sender_user_id", v.sender_user_id},
        {"sender_address", v.sender_address},
        {"sender_device_id", v.sender_device_id},
        {"client_message_id", v.client_message_id},
        {"envelope", v.envelope},
        {"created_at", v.created_at},
    };
}

inline void from_json(const nlohmann::json& j, MessageOut& v) {
    j.at("id").get_to(v.id);
    j.at("conversation_id").get_to(v.conversation_id);
    v.sender_user_id = j.value("sender_user_id", "");
    v.sender_address = j.value("sender_address", "");
    j.at("sender_device_id").get_to(v.sender_device_id);
    j.at("client_message_id").get_to(v.client_message_id);

    if (j.contains("envelope")) {
        j.at("envelope").get_to(v.envelope);
    } else if (j.contains("envelope_json")) {
        j.at("envelope_json").get_to(v.envelope);
    }

    j.at("created_at").get_to(v.created_at);
}

inline void to_json(nlohmann::json& j, const MessageSendRequest& v) {
    j = nlohmann::json{{"conversation_id", v.conversation_id}, {"envelope", v.envelope}};
}

inline void from_json(const nlohmann::json& j, MessageSendRequest& v) {
    j.at("conversation_id").get_to(v.conversation_id);
    j.at("envelope").get_to(v.envelope);
}

inline void to_json(nlohmann::json& j, const MessageSendResponse& v) {
    j = nlohmann::json{{"duplicate", v.duplicate}, {"message", v.message}};
}

inline void from_json(const nlohmann::json& j, MessageSendResponse& v) {
    v.duplicate = j.value("duplicate", false);
    j.at("message").get_to(v.message);
}

inline void from_json(const nlohmann::json& j, WsEventMessageNew& v) {
    j.at("message").get_to(v.message);
}

inline void to_json(nlohmann::json& j, const VoiceCallOffer& v) {
    j = nlohmann::json{{"conversation_id", v.conversation_id}};
}

inline void from_json(const nlohmann::json& j, VoiceCallOffer& v) {
    j.at("conversation_id").get_to(v.conversation_id);
}

inline void to_json(nlohmann::json& j, const VoiceCallAccept& v) {
    j = nlohmann::json{{"call_id", v.call_id}};
}

inline void from_json(const nlohmann::json& j, VoiceCallAccept& v) {
    j.at("call_id").get_to(v.call_id);
}

inline void to_json(nlohmann::json& j, const VoiceCallReject& v) {
    j = nlohmann::json{
        {"call_id", v.call_id},
        {"reason", v.reason.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.reason)},
    };
}

inline void from_json(const nlohmann::json& j, VoiceCallReject& v) {
    j.at("call_id").get_to(v.call_id);
    v.reason = j.value("reason", "");
}

inline void to_json(nlohmann::json& j, const VoiceCallEnd& v) {
    j = nlohmann::json{
        {"call_id", v.call_id},
        {"reason", v.reason.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.reason)},
    };
}

inline void from_json(const nlohmann::json& j, VoiceCallEnd& v) {
    j.at("call_id").get_to(v.call_id);
    v.reason = j.value("reason", "");
}

inline void to_json(nlohmann::json& j, const VoiceAudioChunk& v) {
    j = nlohmann::json{
        {"call_id", v.call_id},
        {"sequence", v.sequence},
        {"pcm_b64", v.pcm_b64},
    };
}

inline void from_json(const nlohmann::json& j, VoiceAudioChunk& v) {
    j.at("call_id").get_to(v.call_id);
    v.sequence = j.value("sequence", 0);
    j.at("pcm_b64").get_to(v.pcm_b64);
}

inline void from_json(const nlohmann::json& j, WsEventCallIncoming& v) {
    j.at("call_id").get_to(v.call_id);
    j.at("conversation_id").get_to(v.conversation_id);
    v.from_user_id = j.value("from_user_id", "");
    v.from_user_address = j.value("from_user_address", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallRinging& v) {
    j.at("call_id").get_to(v.call_id);
    j.at("conversation_id").get_to(v.conversation_id);
    v.peer_user_id = j.value("peer_user_id", "");
    v.peer_user_address = j.value("peer_user_address", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallAccepted& v) {
    j.at("call_id").get_to(v.call_id);
    j.at("conversation_id").get_to(v.conversation_id);
    v.peer_user_id = j.value("peer_user_id", "");
    v.peer_user_address = j.value("peer_user_address", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallRejected& v) {
    v.call_id = j.value("call_id", "");
    v.conversation_id = j.value("conversation_id", "");
    v.reason = j.value("reason", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallBusy& v) {
    v.conversation_id = j.value("conversation_id", "");
    v.reason = j.value("reason", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallEnded& v) {
    j.at("call_id").get_to(v.call_id);
    v.reason = j.value("reason", "");
    v.by_user_id = j.value("by_user_id", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallAudio& v) {
    j.at("call_id").get_to(v.call_id);
    v.from_user_id = j.value("from_user_id", "");
    v.from_user_address = j.value("from_user_address", "");
    v.sequence = j.value("sequence", 0);
    j.at("pcm_b64").get_to(v.pcm_b64);
}

inline void from_json(const nlohmann::json& j, WsEventCallError& v) {
    v.code = j.value("code", "");
    v.detail = j.value("detail", "");
}

}  // namespace blackwire
