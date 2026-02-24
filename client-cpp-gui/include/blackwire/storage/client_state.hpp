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
    std::string rendered_text;
    std::string plaintext;
    std::string plaintext_cache_b64;
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
                       {"rendered_text", v.rendered_text},
                       {"plaintext_cache_b64", v.plaintext_cache_b64.empty() ? nlohmann::json(nullptr)
                                                                             : nlohmann::json(v.plaintext_cache_b64)}};
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
    j.at("id").get_to(v.id);
    j.at("conversation_id").get_to(v.conversation_id);
    j.at("sender_user_id").get_to(v.sender_user_id);
    v.sender_address = j.value("sender_address", "");
    j.at("created_at").get_to(v.created_at);
    // Scrub any historical plaintext payload that may have been persisted.
    v.rendered_text = "[encrypted message]";
    // Never deserialize historical plaintext from disk into runtime state.
    v.plaintext.clear();
    if (j.contains("plaintext_cache_b64") && !j.at("plaintext_cache_b64").is_null()) {
        v.plaintext_cache_b64 = j.value("plaintext_cache_b64", "");
    } else {
        v.plaintext_cache_b64.clear();
    }
}

inline void from_json(const nlohmann::json& j, ConversationMeta& v) {
    v.peer_username = j.value("peer_username", "");
    v.peer_address = j.value("peer_address", "");
    if (j.contains("last_preview")) {
        const auto preview = j.value("last_preview", "");
        v.last_preview = preview.empty() ? "" : "(encrypted message)";
    } else {
        v.last_preview = "";
    }
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
        {"presence_status", v.presence_status},
    };
}

inline void from_json(const nlohmann::json& j, SocialPreferences& v) {
    v.accept_messages_from_strangers = j.value("accept_messages_from_strangers", true);
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
                       {"blocked_conversation_ids", v.blocked_conversation_ids}};
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
}

}  // namespace blackwire
