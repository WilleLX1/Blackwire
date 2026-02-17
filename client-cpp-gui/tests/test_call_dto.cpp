#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "blackwire/models/dto.hpp"

TEST(CallDtoTest, ParsesIncomingAndAcceptedEvents) {
    const auto incoming_json = nlohmann::json::parse(
        R"({"type":"call.incoming","call_id":"call-1","conversation_id":"conv-1","from_user_id":"alice"})");
    const auto accepted_json = nlohmann::json::parse(
        R"({"type":"call.accepted","call_id":"call-1","conversation_id":"conv-1","peer_user_id":"bob"})");

    const auto incoming = incoming_json.get<blackwire::WsEventCallIncoming>();
    const auto accepted = accepted_json.get<blackwire::WsEventCallAccepted>();

    EXPECT_EQ(incoming.call_id, "call-1");
    EXPECT_EQ(incoming.conversation_id, "conv-1");
    EXPECT_EQ(incoming.from_user_id, "alice");
    EXPECT_EQ(accepted.call_id, "call-1");
    EXPECT_EQ(accepted.peer_user_id, "bob");
}

TEST(CallDtoTest, SerializesCallAudioChunk) {
    blackwire::VoiceAudioChunk chunk;
    chunk.call_id = "call-2";
    chunk.sequence = 9;
    chunk.pcm_b64 = "AAECAw==";

    const nlohmann::json json = chunk;
    EXPECT_EQ(json.at("call_id").get<std::string>(), "call-2");
    EXPECT_EQ(json.at("sequence").get<int>(), 9);
    EXPECT_EQ(json.at("pcm_b64").get<std::string>(), "AAECAw==");
}

TEST(CallDtoTest, ParsesAddressFieldsWhenPresent) {
    const auto incoming_json = nlohmann::json::parse(
        R"({"type":"call.incoming","call_id":"call-3","conversation_id":"conv-2","from_user_address":"alice@peer.onion"})");
    const auto ringing_json = nlohmann::json::parse(
        R"({"type":"call.ringing","call_id":"call-3","conversation_id":"conv-2","peer_user_address":"bob@peer.onion"})");

    const auto incoming = incoming_json.get<blackwire::WsEventCallIncoming>();
    const auto ringing = ringing_json.get<blackwire::WsEventCallRinging>();
    EXPECT_EQ(incoming.from_user_address, "alice@peer.onion");
    EXPECT_EQ(ringing.peer_user_address, "bob@peer.onion");
}
