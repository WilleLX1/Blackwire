#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "blackwire/models/dto.hpp"

namespace blackwire {

struct LocalMessage {
    std::string id;
    std::string conversation_id;
    std::string sender_user_id;
    std::string sender_address;
    std::string created_at;
    long long sent_at_ms = 0;
    std::string rendered_text;
    std::string plaintext;
    std::string plaintext_cache_b64;
    std::string attachment_name;
    std::string attachment_mime_type;
    std::string attachment_media_kind;
    std::string attachment_status = "success";
    std::string retry_payload;
};

struct ConversationMeta {
    std::string peer_username;
    std::string peer_address;
    std::string last_preview;
    std::string last_activity_at;
};

struct AudioPreferences {
    std::string preferred_input_device_id;
    std::string preferred_output_device_id;
};

struct SocialPreferences {
    bool accept_messages_from_strangers = true;
    bool save_message_cache = true;
    std::string presence_status = "active";
};

struct ClientState {
    std::string base_url;
    UserOut user;
    bool has_user = false;
    DeviceOut device;
    bool has_device = false;
    std::vector<ConversationOut> conversations;
    std::map<std::string, ConversationMeta> conversation_meta;
    std::map<std::string, std::vector<LocalMessage>> local_messages;
    std::set<std::string> seen_message_ids;
    std::map<std::string, std::string> pinned_sender_sign_keys_by_device_uid;
    std::map<std::string, std::string> last_verified_chain_hash_by_conversation_sender;
    AudioPreferences audio_preferences;
    SocialPreferences social_preferences;
    std::set<std::string> blocked_conversation_ids;
    std::set<std::string> dismissed_conversation_ids;

    bool MarkMessageSeen(const std::string& message_id) {
        const auto inserted = seen_message_ids.insert(message_id);
        return inserted.second;
    }
};

inline void to_json(nlohmann::json& j, const LocalMessage& v) {
    j = nlohmann::json{{"id", v.id},
                       {"conversation_id", v.conversation_id},
                       {"sender_user_id", v.sender_user_id},
                       {"sender_address", v.sender_address.empty() ? nlohmann::json(nullptr)
                                                                   : nlohmann::json(v.sender_address)},
                       {"created_at", v.created_at},
                       {"sent_at_ms", v.sent_at_ms},
                       {"rendered_text", v.rendered_text},
                       {"plaintext_cache_b64", v.plaintext_cache_b64.empty() ? nlohmann::json(nullptr)
                                                                             : nlohmann::json(v.plaintext_cache_b64)},
                       {"attachment_name", v.attachment_name.empty() ? nlohmann::json(nullptr)
                                                                      : nlohmann::json(v.attachment_name)},
                       {"attachment_mime_type", v.attachment_mime_type.empty() ? nlohmann::json(nullptr)
                                                                                : nlohmann::json(v.attachment_mime_type)},
                       {"attachment_media_kind", v.attachment_media_kind.empty() ? nlohmann::json(nullptr)
                                                                                  : nlohmann::json(v.attachment_media_kind)},
                       {"attachment_status", v.attachment_status},
                       {"retry_payload", v.retry_payload.empty() ? nlohmann::json(nullptr)
                                                                 : nlohmann::json(v.retry_payload)}};
}

inline void to_json(nlohmann::json& j, const ConversationMeta& v) {
    j = nlohmann::json{
        {"peer_username", v.peer_username},
        {"peer_address", v.peer_address},
        {"last_preview", v.last_preview},
        {"last_activity_at", v.last_activity_at},
    };
}

inline void from_json(const nlohmann::json& j, LocalMessage& v) {
    v.id = j.value("id", "");
    v.conversation_id = j.value("conversation_id", "");
    v.sender_user_id = j.value("sender_user_id", "");
    v.sender_address = JsonStringOrDefault(j, "sender_address");
    v.created_at = j.value("created_at", "");
    v.sent_at_ms = j.value("sent_at_ms", 0LL);
    // Scrub any historical plaintext payload that may have been persisted.
    v.rendered_text = "[encrypted message]";
    // Never deserialize historical plaintext from disk into runtime state.
    v.plaintext.clear();
    v.plaintext_cache_b64 = JsonStringOrDefault(j, "plaintext_cache_b64");
    v.attachment_name = JsonStringOrDefault(j, "attachment_name");
    v.attachment_mime_type = JsonStringOrDefault(j, "attachment_mime_type");
    v.attachment_media_kind = JsonStringOrDefault(j, "attachment_media_kind");
    v.attachment_status = JsonStringOrDefault(j, "attachment_status", "success");
    v.retry_payload = JsonStringOrDefault(j, "retry_payload");
}

inline void from_json(const nlohmann::json& j, ConversationMeta& v) {
    v.peer_username = j.value("peer_username", "");
    v.peer_address = j.value("peer_address", "");
    v.last_preview = j.value("last_preview", "");
    v.last_activity_at = j.value("last_activity_at", "");
}

inline void to_json(nlohmann::json& j, const AudioPreferences& v) {
    j = nlohmann::json{
        {"preferred_input_device_id", v.preferred_input_device_id},
        {"preferred_output_device_id", v.preferred_output_device_id},
    };
}

inline void from_json(const nlohmann::json& j, AudioPreferences& v) {
    v.preferred_input_device_id = j.value("preferred_input_device_id", "");
    v.preferred_output_device_id = j.value("preferred_output_device_id", "");
}

inline void to_json(nlohmann::json& j, const SocialPreferences& v) {
    j = nlohmann::json{
        {"accept_messages_from_strangers", v.accept_messages_from_strangers},
        {"save_message_cache", v.save_message_cache},
        {"presence_status", v.presence_status},
    };
}

inline void from_json(const nlohmann::json& j, SocialPreferences& v) {
    v.accept_messages_from_strangers = j.value("accept_messages_from_strangers", true);
    v.save_message_cache = j.value("save_message_cache", true);
    v.presence_status = j.value("presence_status", "active");
}

inline void to_json(nlohmann::json& j, const ClientState& v) {
    j = nlohmann::json{{"base_url", v.base_url},
                       {"has_user", v.has_user},
                       {"has_device", v.has_device},
                       {"conversations", v.conversations},
                       {"conversation_meta", v.conversation_meta},
                       {"local_messages", v.local_messages},
                       {"seen_message_ids", v.seen_message_ids},
                       {"pinned_sender_sign_keys_by_device_uid", v.pinned_sender_sign_keys_by_device_uid},
                       {"last_verified_chain_hash_by_conversation_sender", v.last_verified_chain_hash_by_conversation_sender},
                       {"audio_preferences", v.audio_preferences},
                       {"social_preferences", v.social_preferences},
                       {"blocked_conversation_ids", v.blocked_conversation_ids},
                       {"dismissed_conversation_ids", v.dismissed_conversation_ids}};
    if (v.has_user) {
        j["user"] = v.user;
    }
    if (v.has_device) {
        j["device"] = v.device;
    }
}

inline void from_json(const nlohmann::json& j, ClientState& v) {
    v.base_url = j.value("base_url", "http://localhost:8000");
    v.has_user = j.value("has_user", false);
    v.has_device = j.value("has_device", false);

    if (v.has_user && j.contains("user")) {
        j.at("user").get_to(v.user);
    }
    if (v.has_device && j.contains("device")) {
        j.at("device").get_to(v.device);
    }

    if (j.contains("conversations")) {
        j.at("conversations").get_to(v.conversations);
    }
    if (j.contains("conversation_meta")) {
        j.at("conversation_meta").get_to(v.conversation_meta);
    }
    if (j.contains("local_messages")) {
        j.at("local_messages").get_to(v.local_messages);
    }
    if (j.contains("seen_message_ids")) {
        j.at("seen_message_ids").get_to(v.seen_message_ids);
    }
    if (j.contains("pinned_sender_sign_keys_by_device_uid")) {
        j.at("pinned_sender_sign_keys_by_device_uid").get_to(v.pinned_sender_sign_keys_by_device_uid);
    }
    if (j.contains("last_verified_chain_hash_by_conversation_sender")) {
        j.at("last_verified_chain_hash_by_conversation_sender").get_to(v.last_verified_chain_hash_by_conversation_sender);
    }
    if (j.contains("audio_preferences")) {
        j.at("audio_preferences").get_to(v.audio_preferences);
    }
    if (j.contains("social_preferences")) {
        j.at("social_preferences").get_to(v.social_preferences);
    }
    if (j.contains("blocked_conversation_ids")) {
        j.at("blocked_conversation_ids").get_to(v.blocked_conversation_ids);
    }
    if (j.contains("dismissed_conversation_ids")) {
        j.at("dismissed_conversation_ids").get_to(v.dismissed_conversation_ids);
    }
}

}  // namespace blackwire
