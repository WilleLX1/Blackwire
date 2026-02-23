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
    std::string bootstrap_token;
    int bootstrap_expires_in = 0;
    std::string device_uid;
};

struct AuthResponse {
    UserOut user;
    TokenBundle tokens;
};

struct DeviceRegisterRequest {
    std::string label;
    std::string ik_ed25519_pub;
    std::string enc_x25519_pub;
    std::string pub_sign_key;
    std::string pub_dh_key;
};

struct DeviceOut {
    std::string id;
    std::string device_uid;
    std::string user_id;
    std::string label;
    std::string ik_ed25519_pub;
    std::string enc_x25519_pub;
    std::string status;
    std::string last_seen_at;
    std::string revoked_at;
    std::string created_at;
};

struct UserDeviceLookup {
    std::string username;
    std::string peer_address;
    DeviceOut device;
    std::vector<DeviceOut> devices;
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
    std::string recipient_user_address;
    std::string recipient_device_uid;
    std::string ciphertext_b64;
    std::string aad_b64;
    std::string client_message_id;
    std::string signature_b64;
    std::string sender_device_pubkey;
};

struct MessageOut {
    std::string id;
    std::string conversation_id;
    std::string sender_user_id;
    std::string sender_address;
    std::string sender_device_id;
    std::string sender_device_uid;
    std::string sender_device_pubkey;
    std::string client_message_id;
    long long sent_at_ms = 0;
    std::string sender_prev_hash;
    std::string sender_chain_hash;
    CipherEnvelope envelope;
    std::string created_at;
};

struct MessageSendRequest {
    std::string conversation_id;
    CipherEnvelope envelope;
    std::string client_message_id;
    long long sent_at_ms = 0;
    std::string sender_prev_hash;
    std::string sender_chain_hash;
    std::vector<CipherEnvelope> envelopes;
};

struct MessageSendResponse {
    bool duplicate = false;
    MessageOut message;
};

struct WsEventMessageNew {
    std::string copy_id;
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
        {"bootstrap_token", v.bootstrap_token},
        {"bootstrap_expires_in", v.bootstrap_expires_in},
        {"device_uid", v.device_uid},
    };
}

inline void from_json(const nlohmann::json& j, TokenBundle& v) {
    v.access_token = j.value("access_token", "");
    v.token_type = j.value("token_type", "bearer");
    v.access_expires_in = j.value("access_expires_in", 0);
    v.refresh_token = j.value("refresh_token", "");
    v.refresh_expires_in = j.value("refresh_expires_in", 0);
    v.bootstrap_token = j.value("bootstrap_token", "");
    v.bootstrap_expires_in = j.value("bootstrap_expires_in", 0);
    v.device_uid = j.value("device_uid", "");
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
        {"pub_sign_key", v.pub_sign_key.empty() ? v.ik_ed25519_pub : v.pub_sign_key},
        {"pub_dh_key", v.pub_dh_key.empty() ? v.enc_x25519_pub : v.pub_dh_key},
    };
}

inline void from_json(const nlohmann::json& j, DeviceRegisterRequest& v) {
    j.at("label").get_to(v.label);
    v.ik_ed25519_pub = j.value("ik_ed25519_pub", j.value("pub_sign_key", ""));
    v.enc_x25519_pub = j.value("enc_x25519_pub", j.value("pub_dh_key", ""));
    v.pub_sign_key = j.value("pub_sign_key", v.ik_ed25519_pub);
    v.pub_dh_key = j.value("pub_dh_key", v.enc_x25519_pub);
}

inline void to_json(nlohmann::json& j, const DeviceOut& v) {
    j = nlohmann::json{
        {"id", v.id.empty() ? v.device_uid : v.id},
        {"device_uid", v.device_uid.empty() ? v.id : v.device_uid},
        {"user_id", v.user_id},
        {"label", v.label},
        {"ik_ed25519_pub", v.ik_ed25519_pub},
        {"enc_x25519_pub", v.enc_x25519_pub},
        {"pub_sign_key", v.ik_ed25519_pub},
        {"pub_dh_key", v.enc_x25519_pub},
        {"status", v.status},
        {"last_seen_at", v.last_seen_at},
        {"revoked_at", v.revoked_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.revoked_at)},
        {"created_at", v.created_at},
    };
}

inline void from_json(const nlohmann::json& j, DeviceOut& v) {
    v.id = j.value("id", j.value("device_uid", ""));
    v.device_uid = j.value("device_uid", v.id);
    j.at("user_id").get_to(v.user_id);
    j.at("label").get_to(v.label);
    v.ik_ed25519_pub = j.value("ik_ed25519_pub", j.value("pub_sign_key", ""));
    v.enc_x25519_pub = j.value("enc_x25519_pub", j.value("pub_dh_key", ""));
    v.status = j.value("status", "active");
    v.last_seen_at = j.value("last_seen_at", "");
    if (j.contains("revoked_at") && !j.at("revoked_at").is_null()) {
        v.revoked_at = j.value("revoked_at", "");
    } else {
        v.revoked_at.clear();
    }
    j.at("created_at").get_to(v.created_at);
}

inline void to_json(nlohmann::json& j, const UserDeviceLookup& v) {
    j = nlohmann::json{
        {"username", v.username},
        {"peer_address", v.peer_address},
        {"device", v.device},
        {"devices", v.devices.empty() ? nlohmann::json::array({v.device}) : nlohmann::json(v.devices)},
    };
}

inline void from_json(const nlohmann::json& j, UserDeviceLookup& v) {
    j.at("username").get_to(v.username);
    v.peer_address = j.value("peer_address", "");
    if (j.contains("device")) {
        j.at("device").get_to(v.device);
    }
    if (j.contains("devices")) {
        j.at("devices").get_to(v.devices);
    } else if (!v.device.id.empty() || !v.device.device_uid.empty()) {
        v.devices = {v.device};
    } else {
        v.devices.clear();
    }
    if (v.device.id.empty() && !v.devices.empty()) {
        v.device = v.devices.front();
    }
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
    j = nlohmann::json::object();
    if (!v.recipient_user_address.empty() || !v.recipient_device_uid.empty() || !v.signature_b64.empty()) {
        j["recipient_user_address"] = v.recipient_user_address;
        j["recipient_device_uid"] = v.recipient_device_uid.empty() ? v.recipient_device_id : v.recipient_device_uid;
        j["ciphertext_b64"] = v.ciphertext_b64;
        j["aad_b64"] = v.aad_b64.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.aad_b64);
        j["signature_b64"] = v.signature_b64;
        j["sender_device_pubkey"] = v.sender_device_pubkey;
        return;
    }

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
    v.recipient_device_id = j.value("recipient_device_id", j.value("recipient_device_uid", ""));
    v.recipient_device_uid = j.value("recipient_device_uid", v.recipient_device_id);
    v.recipient_user_address = j.value("recipient_user_address", "");
    j.at("ciphertext_b64").get_to(v.ciphertext_b64);
    if (j.contains("aad_b64") && !j.at("aad_b64").is_null()) {
        j.at("aad_b64").get_to(v.aad_b64);
    } else {
        v.aad_b64.clear();
    }
    v.client_message_id = j.value("client_message_id", "");
    v.signature_b64 = j.value("signature_b64", "");
    v.sender_device_pubkey = j.value("sender_device_pubkey", "");
}

inline void to_json(nlohmann::json& j, const MessageOut& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"conversation_id", v.conversation_id},
        {"sender_user_id", v.sender_user_id},
        {"sender_address", v.sender_address},
        {"sender_device_id", v.sender_device_id.empty() ? v.sender_device_uid : v.sender_device_id},
        {"sender_device_uid", v.sender_device_uid.empty() ? v.sender_device_id : v.sender_device_uid},
        {"sender_device_pubkey", v.sender_device_pubkey},
        {"client_message_id", v.client_message_id},
        {"sent_at_ms", v.sent_at_ms},
        {"sender_prev_hash", v.sender_prev_hash},
        {"sender_chain_hash", v.sender_chain_hash},
        {"envelope", v.envelope},
        {"created_at", v.created_at},
    };
}

inline void from_json(const nlohmann::json& j, MessageOut& v) {
    j.at("id").get_to(v.id);
    j.at("conversation_id").get_to(v.conversation_id);
    v.sender_user_id = j.value("sender_user_id", "");
    v.sender_address = j.value("sender_address", "");
    v.sender_device_id = j.value("sender_device_id", j.value("sender_device_uid", ""));
    v.sender_device_uid = j.value("sender_device_uid", v.sender_device_id);
    v.sender_device_pubkey = j.value("sender_device_pubkey", "");
    j.at("client_message_id").get_to(v.client_message_id);
    v.sent_at_ms = j.value("sent_at_ms", 0LL);
    v.sender_prev_hash = j.value("sender_prev_hash", "");
    v.sender_chain_hash = j.value("sender_chain_hash", "");

    if (j.contains("envelope")) {
        j.at("envelope").get_to(v.envelope);
    } else if (j.contains("envelope_json")) {
        j.at("envelope_json").get_to(v.envelope);
    }

    j.at("created_at").get_to(v.created_at);
}

inline void to_json(nlohmann::json& j, const MessageSendRequest& v) {
    if (!v.envelopes.empty()) {
        j = nlohmann::json{
            {"conversation_id", v.conversation_id},
            {"client_message_id", v.client_message_id},
            {"sent_at_ms", v.sent_at_ms},
            {"sender_prev_hash", v.sender_prev_hash},
            {"sender_chain_hash", v.sender_chain_hash},
            {"envelopes", v.envelopes},
        };
        return;
    }
    j = nlohmann::json{{"conversation_id", v.conversation_id}, {"envelope", v.envelope}};
}

inline void from_json(const nlohmann::json& j, MessageSendRequest& v) {
    j.at("conversation_id").get_to(v.conversation_id);
    if (j.contains("envelopes")) {
        j.at("envelopes").get_to(v.envelopes);
    } else if (j.contains("envelope")) {
        j.at("envelope").get_to(v.envelope);
    }
    v.client_message_id = j.value("client_message_id", "");
    v.sent_at_ms = j.value("sent_at_ms", 0LL);
    v.sender_prev_hash = j.value("sender_prev_hash", "");
    v.sender_chain_hash = j.value("sender_chain_hash", "");
}

inline void to_json(nlohmann::json& j, const MessageSendResponse& v) {
    j = nlohmann::json{{"duplicate", v.duplicate}, {"message", v.message}};
}

inline void from_json(const nlohmann::json& j, MessageSendResponse& v) {
    v.duplicate = j.value("duplicate", false);
    j.at("message").get_to(v.message);
}

inline void from_json(const nlohmann::json& j, WsEventMessageNew& v) {
    v.copy_id = j.value("copy_id", "");
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
