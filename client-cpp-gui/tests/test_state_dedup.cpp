#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "blackwire/storage/client_state.hpp"

TEST(StateDedupTest, MarkMessageSeenReturnsFalseForDuplicates) {
    blackwire::ClientState state;

    EXPECT_TRUE(state.MarkMessageSeen("id-1"));
    EXPECT_FALSE(state.MarkMessageSeen("id-1"));
    EXPECT_TRUE(state.MarkMessageSeen("id-2"));
}

TEST(StateDedupTest, ConversationMetaRoundTrip) {
    blackwire::ClientState state;
    blackwire::ConversationMeta initial_meta;
    initial_meta.peer_username = "alice";
    initial_meta.last_preview = "hello";
    initial_meta.last_activity_at = "2026-02-14T12:00:00Z";
    state.conversation_meta["conv-1"] = initial_meta;

    nlohmann::json payload = state;
    const auto restored = payload.get<blackwire::ClientState>();

    ASSERT_TRUE(restored.conversation_meta.contains("conv-1"));
    const auto& loaded_meta = restored.conversation_meta.at("conv-1");
    EXPECT_EQ(loaded_meta.peer_username, "alice");
    EXPECT_EQ(loaded_meta.last_preview, "[encrypted message]");
    EXPECT_EQ(loaded_meta.last_activity_at, "2026-02-14T12:00:00Z");
}
