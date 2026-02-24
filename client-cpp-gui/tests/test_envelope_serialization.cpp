#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "blackwire/models/dto.hpp"

TEST(EnvelopeSerializationTest, RoundTripPreservesFields) {
    blackwire::MessageSendRequest request;
    request.conversation_id = "conv-1";
    request.envelope.version = 1;
    request.envelope.alg = "libsodium-sealedbox-v1";
    request.envelope.recipient_device_id = "device-1";
    request.envelope.ciphertext_b64 = "Zm9v";
    request.envelope.client_message_id = "msg-1";

    const nlohmann::json json = request;
    const auto restored = json.get<blackwire::MessageSendRequest>();

    EXPECT_EQ(restored.conversation_id, request.conversation_id);
    EXPECT_EQ(restored.envelope.alg, request.envelope.alg);
    EXPECT_EQ(restored.envelope.recipient_device_id, request.envelope.recipient_device_id);
    EXPECT_EQ(restored.envelope.ciphertext_b64, request.envelope.ciphertext_b64);
    EXPECT_EQ(restored.envelope.client_message_id, request.envelope.client_message_id);
}

TEST(EnvelopeSerializationTest, V2MessageSendSerializationRoundTrip) {
    blackwire::MessageSendRequest request;
    request.conversation_id = "conv-v2";
    request.client_message_id = "client-v2";
    request.sent_at_ms = 12345;
    request.sender_prev_hash = "";
    request.sender_chain_hash = "abc123";

    blackwire::CipherEnvelope env;
    env.recipient_user_address = "bob@peer.onion";
    env.recipient_device_uid = "device-bob";
    env.ciphertext_b64 = "Zm9v";
    env.aad_b64.clear();
    env.signature_b64 = "c2ln";
    env.sender_device_pubkey = "cHVi";
    request.envelopes = {env};

    const nlohmann::json json = request;
    const auto restored = json.get<blackwire::MessageSendRequest>();

    ASSERT_EQ(restored.envelopes.size(), 1);
    EXPECT_EQ(restored.client_message_id, "client-v2");
    EXPECT_EQ(restored.envelopes.front().recipient_device_uid, "device-bob");
}

TEST(EnvelopeSerializationTest, UserOutParsesCanonicalIdentityWhenPresent) {
    const auto json = nlohmann::json::parse(
        R"({"id":"u1","username":"alice","created_at":"2026-02-17T00:00:00Z","user_address":"alice@peer.onion","home_server_onion":"peer.onion"})");

    const auto user = json.get<blackwire::UserOut>();
    EXPECT_EQ(user.user_address, "alice@peer.onion");
    EXPECT_EQ(user.home_server_onion, "peer.onion");
}

TEST(EnvelopeSerializationTest, UserOutDefaultsCanonicalIdentityWhenMissing) {
    const auto json = nlohmann::json::parse(
        R"({"id":"u1","username":"alice","created_at":"2026-02-17T00:00:00Z"})");

    const auto user = json.get<blackwire::UserOut>();
    EXPECT_TRUE(user.user_address.empty());
    EXPECT_TRUE(user.home_server_onion.empty());
}

TEST(EnvelopeSerializationTest, ConversationOutParsesPeerUsernameWhenPresent) {
    const auto json = nlohmann::json::parse(
        R"({"id":"conv-1","user_a_id":"u1","user_b_id":"u2","created_at":"2026-02-17T00:00:00Z","peer_username":"bob"})");

    const auto conversation = json.get<blackwire::ConversationOut>();
    EXPECT_EQ(conversation.peer_username, "bob");
}

TEST(EnvelopeSerializationTest, ConversationOutDefaultsPeerUsernameWhenMissing) {
    const auto json = nlohmann::json::parse(
        R"({"id":"conv-1","user_a_id":"u1","user_b_id":"u2","created_at":"2026-02-17T00:00:00Z"})");

    const auto conversation = json.get<blackwire::ConversationOut>();
    EXPECT_TRUE(conversation.peer_username.empty());
}

TEST(EnvelopeSerializationTest, ConversationOutParsesPeerAddressWhenPresent) {
    const auto json = nlohmann::json::parse(
        R"({"id":"conv-2","created_at":"2026-02-17T00:00:00Z","peer_address":"bob@peer.onion"})");

    const auto conversation = json.get<blackwire::ConversationOut>();
    EXPECT_EQ(conversation.peer_address, "bob@peer.onion");
}

TEST(EnvelopeSerializationTest, MessageOutDefaultsSenderAddressWhenMissing) {
    const auto json = nlohmann::json::parse(
        R"({"id":"m1","conversation_id":"c1","sender_user_id":"u1","sender_device_id":"d1","client_message_id":"cm1","envelope":{"version":1,"alg":"libsodium-sealedbox-v1","recipient_device_id":"d2","ciphertext_b64":"Zm9v","aad_b64":null,"client_message_id":"cm1"},"created_at":"2026-02-17T00:00:00Z"})");
    const auto message = json.get<blackwire::MessageOut>();
    EXPECT_TRUE(message.sender_address.empty());
}

TEST(EnvelopeSerializationTest, ConversationMemberOutParsesNullableFields) {
    const auto json = nlohmann::json::parse(
        R"({"id":"m1","member_user_id":null,"member_address":"alice@local.invalid","member_server_onion":"local.invalid","role":"member","status":"invited","invited_by_address":"bob@local.invalid","invited_at":"2026-02-24T10:00:00Z","joined_at":null,"left_at":null,"updated_at":"2026-02-24T10:00:00Z"})");

    const auto member = json.get<blackwire::ConversationMemberOut>();
    EXPECT_EQ(member.id, "m1");
    EXPECT_TRUE(member.member_user_id.empty());
    EXPECT_EQ(member.member_address, "alice@local.invalid");
    EXPECT_TRUE(member.joined_at.empty());
    EXPECT_TRUE(member.left_at.empty());
}
