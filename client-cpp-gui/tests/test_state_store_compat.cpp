#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "blackwire/storage/client_state.hpp"

TEST(StateCompatTest, LoadsLegacyLocalMessageWithoutPlaintext) {
    const auto json = nlohmann::json::parse(
        R"({"id":"m1","conversation_id":"c1","sender_user_id":"u1","created_at":"2026-02-14T10:00:00Z","rendered_text":"[2026] Me: hi"})");

    const auto message = json.get<blackwire::LocalMessage>();
    EXPECT_EQ(message.id, "m1");
    EXPECT_EQ(message.plaintext, "");
    EXPECT_EQ(message.plaintext_cache_b64, "");
}

TEST(StateCompatTest, DoesNotPersistLocalMessagePlaintextToDisk) {
    blackwire::LocalMessage message;
    message.id = "m2";
    message.conversation_id = "c2";
    message.sender_user_id = "u2";
    message.created_at = "2026-02-14T11:00:00Z";
    message.rendered_text = "[2026] Peer: encrypted";
    message.plaintext = "hello there";

    const nlohmann::json json = message;
    EXPECT_FALSE(json.contains("plaintext"));
    EXPECT_TRUE(json.contains("plaintext_cache_b64"));

    const auto roundtrip = json.get<blackwire::LocalMessage>();
    EXPECT_TRUE(roundtrip.plaintext.empty());
}

TEST(StateCompatTest, LoadsEncryptedPlaintextCacheWithoutPlaintext) {
    const auto json = nlohmann::json::parse(
        R"({"id":"m3","conversation_id":"c3","sender_user_id":"u3","created_at":"2026-02-14T12:00:00Z","rendered_text":"[2026] Me: encrypted","plaintext_cache_b64":"Zm9v"})");

    const auto message = json.get<blackwire::LocalMessage>();
    EXPECT_EQ(message.plaintext, "");
    EXPECT_EQ(message.plaintext_cache_b64, "Zm9v");
}

TEST(StateCompatTest, LoadsLegacyClientStateWithoutAudioPreferences) {
    const auto json = nlohmann::json::parse(
        R"({"base_url":"http://localhost:8000","has_user":false,"has_device":false,"conversations":[]})");

    const auto state = json.get<blackwire::ClientState>();
    EXPECT_TRUE(state.audio_preferences.preferred_input_device_id.empty());
    EXPECT_TRUE(state.audio_preferences.preferred_output_device_id.empty());
}

TEST(StateCompatTest, PersistsAudioPreferencesRoundTrip) {
    blackwire::ClientState state;
    state.base_url = "http://localhost:8000";
    state.audio_preferences.preferred_input_device_id = "input-device-id";
    state.audio_preferences.preferred_output_device_id = "output-device-id";

    const nlohmann::json serialized = state;
    ASSERT_TRUE(serialized.contains("audio_preferences"));
    EXPECT_EQ(
        serialized.at("audio_preferences").at("preferred_input_device_id").get<std::string>(),
        "input-device-id");
    EXPECT_EQ(
        serialized.at("audio_preferences").at("preferred_output_device_id").get<std::string>(),
        "output-device-id");

    const auto roundtrip = serialized.get<blackwire::ClientState>();
    EXPECT_EQ(roundtrip.audio_preferences.preferred_input_device_id, "input-device-id");
    EXPECT_EQ(roundtrip.audio_preferences.preferred_output_device_id, "output-device-id");
}

TEST(StateCompatTest, LoadsLegacyClientStateWithoutSocialPreferences) {
    const auto json = nlohmann::json::parse(
        R"({"base_url":"http://localhost:8000","has_user":false,"has_device":false,"conversations":[]})");

    const auto state = json.get<blackwire::ClientState>();
    EXPECT_TRUE(state.social_preferences.accept_messages_from_strangers);
    EXPECT_TRUE(state.blocked_conversation_ids.empty());
}

TEST(StateCompatTest, PersistsSocialPreferencesAndBlockedConversationsRoundTrip) {
    blackwire::ClientState state;
    state.base_url = "http://localhost:8000";
    state.social_preferences.accept_messages_from_strangers = false;
    state.blocked_conversation_ids.insert("conv-1");
    state.blocked_conversation_ids.insert("conv-2");

    const nlohmann::json serialized = state;
    ASSERT_TRUE(serialized.contains("social_preferences"));
    ASSERT_TRUE(serialized.contains("blocked_conversation_ids"));
    EXPECT_FALSE(
        serialized.at("social_preferences").at("accept_messages_from_strangers").get<bool>());

    const auto roundtrip = serialized.get<blackwire::ClientState>();
    EXPECT_FALSE(roundtrip.social_preferences.accept_messages_from_strangers);
    EXPECT_TRUE(roundtrip.blocked_conversation_ids.contains("conv-1"));
    EXPECT_TRUE(roundtrip.blocked_conversation_ids.contains("conv-2"));
}

TEST(StateCompatTest, ConversationMetaPeerAddressRoundTrip) {
    blackwire::ConversationMeta meta;
    meta.peer_username = "alice";
    meta.peer_address = "alice@peer.onion";
    meta.last_preview = "hi";

    const nlohmann::json json = meta;
    const auto restored = json.get<blackwire::ConversationMeta>();
    EXPECT_EQ(restored.peer_username, "alice");
    EXPECT_EQ(restored.peer_address, "alice@peer.onion");
}
