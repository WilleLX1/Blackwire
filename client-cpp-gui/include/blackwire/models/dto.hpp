#pragma once

#include <optional>
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
    std::vector<std::string> supported_message_modes;
    std::string last_seen_at;
    std::string revoked_at;
    std::string created_at;
};

struct UserDeviceLookup {
    std::string username;
    std::string peer_address;
    DeviceOut device;
    std::vector<DeviceOut> devices;
    long long attachment_inline_max_bytes = 0;
    long long max_ciphertext_bytes = 0;
    std::string attachment_policy_source = "local";
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
    std::string conversation_type = "direct";
    std::string group_uid;
    std::string group_name;
    int member_count = 0;
    std::string membership_state = "none";
    bool can_manage_members = false;
    std::string origin_server_onion;
    std::string owner_address;
};

struct CreateGroupConversationRequest {
    std::string name;
    std::vector<std::string> member_addresses;
};

struct GroupInviteRequest {
    std::vector<std::string> member_addresses;
};

struct GroupRenameRequest {
    std::string name;
};

struct ConversationMemberOut {
    std::string id;
    std::string member_user_id;
    std::string member_address;
    std::string member_server_onion;
    std::string role = "member";
    std::string status = "invited";
    std::string invited_by_address;
    std::string invited_at;
    std::string joined_at;
    std::string left_at;
    std::string updated_at;
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
    nlohmann::json ratchet_header = nullptr;
    nlohmann::json ratchet_init = nullptr;
};

struct MessageOut {
    std::string id;
    std::string conversation_id;
    std::string sender_user_id;
    std::string sender_address;
    std::string sender_device_id;
    std::string sender_device_uid;
    std::string sender_device_pubkey;
    std::string encryption_mode = "sealedbox_v0_2a";
    std::string client_message_id;
    long long sent_at_ms = 0;
    std::string sender_prev_hash;
    std::string sender_chain_hash;
    CipherEnvelope envelope;
    std::string created_at;
};

struct MessageSendRequest {
    std::string conversation_id;
    std::string encryption_mode = "sealedbox_v0_2a";
    CipherEnvelope envelope;
    std::string client_message_id;
    long long sent_at_ms = 0;
    std::string sender_prev_hash;
    std::string sender_chain_hash;
    std::vector<CipherEnvelope> envelopes;
};

struct SignedPrekeyUpload {
    int key_id = 1;
    std::string pub_x25519_b64;
    std::string sig_by_device_sign_key_b64;
    std::string expires_at;
};

struct OneTimePrekeyUpload {
    int key_id = 1;
    std::string pub_x25519_b64;
};

struct PrekeyUploadRequest {
    SignedPrekeyUpload signed_prekey;
    std::vector<OneTimePrekeyUpload> one_time_prekeys;
};

struct PrekeyUploadResponse {
    int uploaded_signed_prekey_key_id = 0;
    int accepted_one_time_prekeys = 0;
};

struct SignedPrekeyOut {
    int key_id = 0;
    std::string pub_x25519_b64;
    std::string sig_by_device_sign_key_b64;
    std::string expires_at;
};

struct OneTimePrekeyOut {
    int key_id = 0;
    std::string pub_x25519_b64;
};

struct ResolvedPrekeyDevice {
    std::string device_uid;
    std::string pub_sign_key;
    std::string pub_dh_key;
    std::vector<std::string> supported_message_modes;
    bool opk_missing = false;
    std::optional<SignedPrekeyOut> signed_prekey;
    std::optional<OneTimePrekeyOut> one_time_prekey;
};

struct ResolvePrekeysResponse {
    std::string username;
    std::string peer_address;
    std::vector<ResolvedPrekeyDevice> devices;
};

struct ConversationRecipientDeviceOut {
    std::string member_address;
    std::string member_status = "active";
    DeviceOut device;
    std::optional<ResolvedPrekeyDevice> prekey;
};

struct ConversationRecipientsOut {
    std::string conversation_id;
    std::string conversation_type = "direct";
    std::vector<ConversationRecipientDeviceOut> recipients;
};

struct PresenceSetRequest {
    std::string status;
};

struct PresenceSetResponse {
    std::string status;
};

struct PresenceResolveRequest {
    std::vector<std::string> peer_addresses;
};

struct PresencePeerOut {
    std::string peer_address;
    std::string status;
};

struct PresenceResolveResponse {
    std::string self_status;
    std::vector<PresencePeerOut> peers;
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

struct VoiceCallWebRtcOffer {
    std::string call_id;
    std::string sdp;
    int call_schema_version = 1;
    std::string call_mode = "webrtc";
    int max_participants = 2;
    std::string target_user_address;
    std::string source_user_address;
};

struct VoiceCallWebRtcAnswer {
    std::string call_id;
    std::string sdp;
    int call_schema_version = 1;
    std::string call_mode = "webrtc";
    int max_participants = 2;
    std::string target_user_address;
    std::string source_user_address;
};

struct VoiceCallWebRtcIce {
    std::string call_id;
    std::string candidate;
    std::string sdp_mid;
    int sdp_mline_index = -1;
    int call_schema_version = 1;
    std::string call_mode = "webrtc";
    int max_participants = 2;
    std::string target_user_address;
    std::string source_user_address;
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
    int call_schema_version = 1;
    std::string call_mode;
    int max_participants = 2;
    nlohmann::json ice_servers = nlohmann::json::array();
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

struct WsEventCallGroupParticipant {
    std::string member_address;
    std::string state;
};

struct WsEventCallGroupState {
    std::string call_id;
    std::string conversation_id;
    std::string group_uid;
    std::string state;
    std::string call_mode;
    int max_participants = 2;
    std::vector<WsEventCallGroupParticipant> participants;
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

struct WsEventCallWebRtcOffer {
    std::string call_id;
    std::string sdp;
    int call_schema_version = 1;
    std::string call_mode = "webrtc";
    int max_participants = 2;
    std::string from_user_address;
    std::string source_user_address;
    std::string target_user_address;
};

struct WsEventCallWebRtcAnswer {
    std::string call_id;
    std::string sdp;
    int call_schema_version = 1;
    std::string call_mode = "webrtc";
    int max_participants = 2;
    std::string from_user_address;
    std::string source_user_address;
    std::string target_user_address;
};

struct WsEventCallWebRtcIce {
    std::string call_id;
    std::string candidate;
    std::string sdp_mid;
    int sdp_mline_index = -1;
    int call_schema_version = 1;
    std::string call_mode = "webrtc";
    int max_participants = 2;
    std::string from_user_address;
    std::string source_user_address;
    std::string target_user_address;
};

struct WsEventGroupRenamed {
    std::string conversation_id;
    std::string group_uid;
    std::string group_name;
    std::string actor_address;
    int event_seq = 0;
};

inline std::string JsonStringOrDefault(
    const nlohmann::json& j,
    const char* key,
    const std::string& fallback = "") {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return fallback;
    }
    try {
        return it->get<std::string>();
    } catch (...) {
        return fallback;
    }
}

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
        {"supported_message_modes", v.supported_message_modes},
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
    v.supported_message_modes = j.value("supported_message_modes", std::vector<std::string>{"sealedbox_v0_2a"});
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
        {"attachment_inline_max_bytes", v.attachment_inline_max_bytes},
        {"max_ciphertext_bytes", v.max_ciphertext_bytes},
        {"attachment_policy_source", v.attachment_policy_source},
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
    v.attachment_inline_max_bytes = j.value("attachment_inline_max_bytes", 0LL);
    v.max_ciphertext_bytes = j.value("max_ciphertext_bytes", 0LL);
    v.attachment_policy_source = j.value("attachment_policy_source", "local");
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
        {"conversation_type", v.conversation_type},
        {"group_uid", v.group_uid.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.group_uid)},
        {"group_name", v.group_name},
        {"member_count", v.member_count},
        {"membership_state", v.membership_state},
        {"can_manage_members", v.can_manage_members},
        {"origin_server_onion", v.origin_server_onion},
        {"owner_address", v.owner_address},
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
    v.conversation_type = j.value("conversation_type", "direct");
    if (j.contains("group_uid") && !j.at("group_uid").is_null()) {
        v.group_uid = j.value("group_uid", "");
    } else {
        v.group_uid.clear();
    }
    v.group_name = j.value("group_name", "");
    v.member_count = j.value("member_count", 0);
    v.membership_state = j.value("membership_state", "none");
    v.can_manage_members = j.value("can_manage_members", false);
    v.origin_server_onion = j.value("origin_server_onion", "");
    v.owner_address = j.value("owner_address", "");
}

inline void to_json(nlohmann::json& j, const CreateGroupConversationRequest& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"member_addresses", v.member_addresses},
    };
}

inline void from_json(const nlohmann::json& j, CreateGroupConversationRequest& v) {
    v.name = j.value("name", "");
    v.member_addresses = j.value("member_addresses", std::vector<std::string>{});
}

inline void to_json(nlohmann::json& j, const GroupInviteRequest& v) {
    j = nlohmann::json{
        {"member_addresses", v.member_addresses},
    };
}

inline void from_json(const nlohmann::json& j, GroupInviteRequest& v) {
    v.member_addresses = j.value("member_addresses", std::vector<std::string>{});
}

inline void to_json(nlohmann::json& j, const GroupRenameRequest& v) {
    j = nlohmann::json{
        {"name", v.name},
    };
}

inline void from_json(const nlohmann::json& j, GroupRenameRequest& v) {
    v.name = j.value("name", "");
}

inline void to_json(nlohmann::json& j, const ConversationMemberOut& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"member_user_id", v.member_user_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.member_user_id)},
        {"member_address", v.member_address},
        {"member_server_onion", v.member_server_onion},
        {"role", v.role},
        {"status", v.status},
        {"invited_by_address", v.invited_by_address},
        {"invited_at", v.invited_at},
        {"joined_at", v.joined_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.joined_at)},
        {"left_at", v.left_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.left_at)},
        {"updated_at", v.updated_at},
    };
}

inline void from_json(const nlohmann::json& j, ConversationMemberOut& v) {
    v.id = JsonStringOrDefault(j, "id");
    v.member_user_id = JsonStringOrDefault(j, "member_user_id");
    v.member_address = JsonStringOrDefault(j, "member_address");
    v.member_server_onion = JsonStringOrDefault(j, "member_server_onion");
    v.role = JsonStringOrDefault(j, "role", "member");
    v.status = JsonStringOrDefault(j, "status", "invited");
    v.invited_by_address = JsonStringOrDefault(j, "invited_by_address");
    v.invited_at = JsonStringOrDefault(j, "invited_at");
    v.joined_at = JsonStringOrDefault(j, "joined_at");
    v.left_at = JsonStringOrDefault(j, "left_at");
    v.updated_at = JsonStringOrDefault(j, "updated_at");
}

inline void to_json(nlohmann::json& j, const ConversationRecipientDeviceOut& v) {
    nlohmann::json prekey_json = nlohmann::json(nullptr);
    if (v.prekey.has_value()) {
        const auto& prekey = v.prekey.value();
        prekey_json = nlohmann::json{
            {"device_uid", prekey.device_uid},
            {"pub_sign_key", prekey.pub_sign_key},
            {"pub_dh_key", prekey.pub_dh_key},
            {"supported_message_modes", prekey.supported_message_modes},
            {"opk_missing", prekey.opk_missing},
        };
        if (prekey.signed_prekey.has_value()) {
            prekey_json["signed_prekey"] = nlohmann::json{
                {"key_id", prekey.signed_prekey->key_id},
                {"pub_x25519_b64", prekey.signed_prekey->pub_x25519_b64},
                {"sig_by_device_sign_key_b64", prekey.signed_prekey->sig_by_device_sign_key_b64},
                {"expires_at", prekey.signed_prekey->expires_at},
            };
        } else {
            prekey_json["signed_prekey"] = nullptr;
        }
        if (prekey.one_time_prekey.has_value()) {
            prekey_json["one_time_prekey"] = nlohmann::json{
                {"key_id", prekey.one_time_prekey->key_id},
                {"pub_x25519_b64", prekey.one_time_prekey->pub_x25519_b64},
            };
        } else {
            prekey_json["one_time_prekey"] = nullptr;
        }
    }
    j = nlohmann::json{
        {"member_address", v.member_address},
        {"member_status", v.member_status},
        {"device", v.device},
        {"prekey", prekey_json},
    };
}

inline void from_json(const nlohmann::json& j, ConversationRecipientDeviceOut& v) {
    v.member_address = j.value("member_address", "");
    v.member_status = j.value("member_status", "active");
    if (j.contains("device") && j.at("device").is_object()) {
        j.at("device").get_to(v.device);
    } else {
        v.device = DeviceOut{};
    }
    if (j.contains("prekey") && !j.at("prekey").is_null()) {
        const auto& prekey_json = j.at("prekey");
        ResolvedPrekeyDevice prekey;
        prekey.device_uid = prekey_json.value("device_uid", "");
        prekey.pub_sign_key = prekey_json.value("pub_sign_key", "");
        prekey.pub_dh_key = prekey_json.value("pub_dh_key", "");
        prekey.supported_message_modes =
            prekey_json.value("supported_message_modes", std::vector<std::string>{"sealedbox_v0_2a"});
        prekey.opk_missing = prekey_json.value("opk_missing", false);
        if (prekey_json.contains("signed_prekey") && !prekey_json.at("signed_prekey").is_null()) {
            SignedPrekeyOut signed_prekey;
            const auto& signed_prekey_json = prekey_json.at("signed_prekey");
            signed_prekey.key_id = signed_prekey_json.value("key_id", 0);
            signed_prekey.pub_x25519_b64 = signed_prekey_json.value("pub_x25519_b64", "");
            signed_prekey.sig_by_device_sign_key_b64 = signed_prekey_json.value("sig_by_device_sign_key_b64", "");
            signed_prekey.expires_at = signed_prekey_json.value("expires_at", "");
            prekey.signed_prekey = signed_prekey;
        } else {
            prekey.signed_prekey.reset();
        }
        if (prekey_json.contains("one_time_prekey") && !prekey_json.at("one_time_prekey").is_null()) {
            OneTimePrekeyOut one_time_prekey;
            const auto& one_time_prekey_json = prekey_json.at("one_time_prekey");
            one_time_prekey.key_id = one_time_prekey_json.value("key_id", 0);
            one_time_prekey.pub_x25519_b64 = one_time_prekey_json.value("pub_x25519_b64", "");
            prekey.one_time_prekey = one_time_prekey;
        } else {
            prekey.one_time_prekey.reset();
        }
        v.prekey = prekey;
    } else {
        v.prekey.reset();
    }
}

inline void to_json(nlohmann::json& j, const ConversationRecipientsOut& v) {
    j = nlohmann::json{
        {"conversation_id", v.conversation_id},
        {"conversation_type", v.conversation_type},
        {"recipients", v.recipients},
    };
}

inline void from_json(const nlohmann::json& j, ConversationRecipientsOut& v) {
    v.conversation_id = j.value("conversation_id", "");
    v.conversation_type = j.value("conversation_type", "direct");
    v.recipients = j.value("recipients", std::vector<ConversationRecipientDeviceOut>{});
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
        if (!v.ratchet_header.is_null()) {
            j["ratchet_header"] = v.ratchet_header;
        }
        if (!v.ratchet_init.is_null()) {
            j["ratchet_init"] = v.ratchet_init;
        }
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
    v.ratchet_header = j.value("ratchet_header", nlohmann::json(nullptr));
    v.ratchet_init = j.value("ratchet_init", nlohmann::json(nullptr));
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
        {"encryption_mode", v.encryption_mode},
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
    v.encryption_mode = j.value("encryption_mode", "sealedbox_v0_2a");
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
            {"encryption_mode", v.encryption_mode},
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
    v.encryption_mode = j.value("encryption_mode", "sealedbox_v0_2a");
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

inline void to_json(nlohmann::json& j, const SignedPrekeyUpload& v) {
    j = nlohmann::json{
        {"key_id", v.key_id},
        {"pub_x25519_b64", v.pub_x25519_b64},
        {"sig_by_device_sign_key_b64", v.sig_by_device_sign_key_b64},
        {"expires_at", v.expires_at},
    };
}

inline void from_json(const nlohmann::json& j, SignedPrekeyUpload& v) {
    v.key_id = j.value("key_id", 1);
    v.pub_x25519_b64 = j.value("pub_x25519_b64", "");
    v.sig_by_device_sign_key_b64 = j.value("sig_by_device_sign_key_b64", "");
    v.expires_at = j.value("expires_at", "");
}

inline void to_json(nlohmann::json& j, const OneTimePrekeyUpload& v) {
    j = nlohmann::json{
        {"key_id", v.key_id},
        {"pub_x25519_b64", v.pub_x25519_b64},
    };
}

inline void from_json(const nlohmann::json& j, OneTimePrekeyUpload& v) {
    v.key_id = j.value("key_id", 1);
    v.pub_x25519_b64 = j.value("pub_x25519_b64", "");
}

inline void to_json(nlohmann::json& j, const PrekeyUploadRequest& v) {
    j = nlohmann::json{
        {"signed_prekey", v.signed_prekey},
        {"one_time_prekeys", v.one_time_prekeys},
    };
}

inline void from_json(const nlohmann::json& j, PrekeyUploadRequest& v) {
    if (j.contains("signed_prekey")) {
        j.at("signed_prekey").get_to(v.signed_prekey);
    }
    if (j.contains("one_time_prekeys")) {
        j.at("one_time_prekeys").get_to(v.one_time_prekeys);
    }
}

inline void to_json(nlohmann::json& j, const PrekeyUploadResponse& v) {
    j = nlohmann::json{
        {"uploaded_signed_prekey_key_id", v.uploaded_signed_prekey_key_id},
        {"accepted_one_time_prekeys", v.accepted_one_time_prekeys},
    };
}

inline void from_json(const nlohmann::json& j, PrekeyUploadResponse& v) {
    v.uploaded_signed_prekey_key_id = j.value("uploaded_signed_prekey_key_id", 0);
    v.accepted_one_time_prekeys = j.value("accepted_one_time_prekeys", 0);
}

inline void to_json(nlohmann::json& j, const SignedPrekeyOut& v) {
    j = nlohmann::json{
        {"key_id", v.key_id},
        {"pub_x25519_b64", v.pub_x25519_b64},
        {"sig_by_device_sign_key_b64", v.sig_by_device_sign_key_b64},
        {"expires_at", v.expires_at},
    };
}

inline void from_json(const nlohmann::json& j, SignedPrekeyOut& v) {
    v.key_id = j.value("key_id", 0);
    v.pub_x25519_b64 = j.value("pub_x25519_b64", "");
    v.sig_by_device_sign_key_b64 = j.value("sig_by_device_sign_key_b64", "");
    v.expires_at = j.value("expires_at", "");
}

inline void to_json(nlohmann::json& j, const OneTimePrekeyOut& v) {
    j = nlohmann::json{
        {"key_id", v.key_id},
        {"pub_x25519_b64", v.pub_x25519_b64},
    };
}

inline void from_json(const nlohmann::json& j, OneTimePrekeyOut& v) {
    v.key_id = j.value("key_id", 0);
    v.pub_x25519_b64 = j.value("pub_x25519_b64", "");
}

inline void to_json(nlohmann::json& j, const ResolvedPrekeyDevice& v) {
    j = nlohmann::json{
        {"device_uid", v.device_uid},
        {"pub_sign_key", v.pub_sign_key},
        {"pub_dh_key", v.pub_dh_key},
        {"supported_message_modes", v.supported_message_modes},
        {"opk_missing", v.opk_missing},
    };
    if (v.signed_prekey.has_value()) {
        j["signed_prekey"] = v.signed_prekey.value();
    } else {
        j["signed_prekey"] = nullptr;
    }
    if (v.one_time_prekey.has_value()) {
        j["one_time_prekey"] = v.one_time_prekey.value();
    } else {
        j["one_time_prekey"] = nullptr;
    }
}

inline void from_json(const nlohmann::json& j, ResolvedPrekeyDevice& v) {
    v.device_uid = j.value("device_uid", "");
    v.pub_sign_key = j.value("pub_sign_key", "");
    v.pub_dh_key = j.value("pub_dh_key", "");
    v.supported_message_modes = j.value("supported_message_modes", std::vector<std::string>{});
    v.opk_missing = j.value("opk_missing", false);
    if (j.contains("signed_prekey") && !j.at("signed_prekey").is_null()) {
        v.signed_prekey = j.at("signed_prekey").get<SignedPrekeyOut>();
    } else {
        v.signed_prekey.reset();
    }
    if (j.contains("one_time_prekey") && !j.at("one_time_prekey").is_null()) {
        v.one_time_prekey = j.at("one_time_prekey").get<OneTimePrekeyOut>();
    } else {
        v.one_time_prekey.reset();
    }
}

inline void to_json(nlohmann::json& j, const ResolvePrekeysResponse& v) {
    j = nlohmann::json{
        {"username", v.username},
        {"peer_address", v.peer_address},
        {"devices", v.devices},
    };
}

inline void from_json(const nlohmann::json& j, ResolvePrekeysResponse& v) {
    v.username = j.value("username", "");
    v.peer_address = j.value("peer_address", "");
    if (j.contains("devices")) {
        j.at("devices").get_to(v.devices);
    } else {
        v.devices.clear();
    }
}

inline void to_json(nlohmann::json& j, const PresenceSetRequest& v) {
    j = nlohmann::json{
        {"status", v.status},
    };
}

inline void from_json(const nlohmann::json& j, PresenceSetRequest& v) {
    v.status = j.value("status", "");
}

inline void to_json(nlohmann::json& j, const PresenceSetResponse& v) {
    j = nlohmann::json{
        {"status", v.status},
    };
}

inline void from_json(const nlohmann::json& j, PresenceSetResponse& v) {
    v.status = j.value("status", "");
}

inline void to_json(nlohmann::json& j, const PresenceResolveRequest& v) {
    j = nlohmann::json{
        {"peer_addresses", v.peer_addresses},
    };
}

inline void from_json(const nlohmann::json& j, PresenceResolveRequest& v) {
    v.peer_addresses = j.value("peer_addresses", std::vector<std::string>{});
}

inline void to_json(nlohmann::json& j, const PresencePeerOut& v) {
    j = nlohmann::json{
        {"peer_address", v.peer_address},
        {"status", v.status},
    };
}

inline void from_json(const nlohmann::json& j, PresencePeerOut& v) {
    v.peer_address = j.value("peer_address", "");
    v.status = j.value("status", "offline");
}

inline void to_json(nlohmann::json& j, const PresenceResolveResponse& v) {
    j = nlohmann::json{
        {"self_status", v.self_status},
        {"peers", v.peers},
    };
}

inline void from_json(const nlohmann::json& j, PresenceResolveResponse& v) {
    v.self_status = j.value("self_status", "offline");
    v.peers = j.value("peers", std::vector<PresencePeerOut>{});
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

inline void to_json(nlohmann::json& j, const VoiceCallWebRtcOffer& v) {
    j = nlohmann::json{
        {"call_id", v.call_id},
        {"sdp", v.sdp},
        {"call_schema_version", v.call_schema_version},
        {"call_mode", v.call_mode},
        {"max_participants", v.max_participants},
        {"target_user_address", v.target_user_address.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(v.target_user_address)},
        {"source_user_address", v.source_user_address.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(v.source_user_address)},
    };
}

inline void from_json(const nlohmann::json& j, VoiceCallWebRtcOffer& v) {
    v.call_id = j.value("call_id", "");
    v.sdp = j.value("sdp", "");
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.target_user_address = j.value("target_user_address", "");
    v.source_user_address = j.value("source_user_address", "");
}

inline void to_json(nlohmann::json& j, const VoiceCallWebRtcAnswer& v) {
    j = nlohmann::json{
        {"call_id", v.call_id},
        {"sdp", v.sdp},
        {"call_schema_version", v.call_schema_version},
        {"call_mode", v.call_mode},
        {"max_participants", v.max_participants},
        {"target_user_address", v.target_user_address.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(v.target_user_address)},
        {"source_user_address", v.source_user_address.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(v.source_user_address)},
    };
}

inline void from_json(const nlohmann::json& j, VoiceCallWebRtcAnswer& v) {
    v.call_id = j.value("call_id", "");
    v.sdp = j.value("sdp", "");
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.target_user_address = j.value("target_user_address", "");
    v.source_user_address = j.value("source_user_address", "");
}

inline void to_json(nlohmann::json& j, const VoiceCallWebRtcIce& v) {
    j = nlohmann::json{
        {"call_id", v.call_id},
        {"candidate", v.candidate},
        {"sdp_mid", v.sdp_mid.empty() ? nlohmann::json(nullptr) : nlohmann::json(v.sdp_mid)},
        {"sdp_mline_index", v.sdp_mline_index < 0 ? nlohmann::json(nullptr) : nlohmann::json(v.sdp_mline_index)},
        {"call_schema_version", v.call_schema_version},
        {"call_mode", v.call_mode},
        {"max_participants", v.max_participants},
        {"target_user_address", v.target_user_address.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(v.target_user_address)},
        {"source_user_address", v.source_user_address.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(v.source_user_address)},
    };
}

inline void from_json(const nlohmann::json& j, VoiceCallWebRtcIce& v) {
    v.call_id = j.value("call_id", "");
    v.candidate = j.value("candidate", "");
    v.sdp_mid = j.value("sdp_mid", "");
    v.sdp_mline_index = j.value("sdp_mline_index", -1);
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.target_user_address = j.value("target_user_address", "");
    v.source_user_address = j.value("source_user_address", "");
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
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "");
    v.max_participants = j.value("max_participants", 2);
    v.ice_servers = j.value("ice_servers", nlohmann::json::array());
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

inline void from_json(const nlohmann::json& j, WsEventCallGroupParticipant& v) {
    v.member_address = j.value("member_address", "");
    v.state = j.value("state", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallGroupState& v) {
    v.call_id = j.value("call_id", "");
    v.conversation_id = j.value("conversation_id", "");
    v.group_uid = j.value("group_uid", "");
    v.state = j.value("state", "");
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.participants = j.value("participants", std::vector<WsEventCallGroupParticipant>{});
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

inline void from_json(const nlohmann::json& j, WsEventCallWebRtcOffer& v) {
    v.call_id = j.value("call_id", "");
    v.sdp = j.value("sdp", "");
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.from_user_address = j.value("from_user_address", j.value("source_user_address", ""));
    v.source_user_address = j.value("source_user_address", v.from_user_address);
    v.target_user_address = j.value("target_user_address", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallWebRtcAnswer& v) {
    v.call_id = j.value("call_id", "");
    v.sdp = j.value("sdp", "");
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.from_user_address = j.value("from_user_address", j.value("source_user_address", ""));
    v.source_user_address = j.value("source_user_address", v.from_user_address);
    v.target_user_address = j.value("target_user_address", "");
}

inline void from_json(const nlohmann::json& j, WsEventCallWebRtcIce& v) {
    v.call_id = j.value("call_id", "");
    v.candidate = j.value("candidate", "");
    v.sdp_mid = j.value("sdp_mid", "");
    v.sdp_mline_index = j.value("sdp_mline_index", -1);
    v.call_schema_version = j.value("call_schema_version", 1);
    v.call_mode = j.value("call_mode", "webrtc");
    v.max_participants = j.value("max_participants", 2);
    v.from_user_address = j.value("from_user_address", j.value("source_user_address", ""));
    v.source_user_address = j.value("source_user_address", v.from_user_address);
    v.target_user_address = j.value("target_user_address", "");
}

inline void from_json(const nlohmann::json& j, WsEventGroupRenamed& v) {
    v.conversation_id = j.value("conversation_id", "");
    v.group_uid = j.value("group_uid", "");
    v.group_name = j.value("group_name", "");
    v.actor_address = j.value("actor_address", "");
    v.event_seq = j.value("event_seq", 0);
}

}  // namespace blackwire
