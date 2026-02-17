#include <gtest/gtest.h>

#include <vector>

#include "blackwire/util/conversation_view.hpp"

TEST(ConversationViewTest, UsesPeerUsernameWhenKnown) {
    const auto title = blackwire::BuildConversationTitle("alice", "12345678-abcd", "localhost:8000");
    EXPECT_EQ(title.toStdString(), "alice@localhost:8000");
}

TEST(ConversationViewTest, KeepsCanonicalAddressWhenProvided) {
    const auto title = blackwire::BuildConversationTitle("alice@abcd1234.onion", "12345678-abcd", "localhost:8000");
    EXPECT_EQ(title.toStdString(), "alice@abcd1234.onion");
}

TEST(ConversationViewTest, FallsBackToShortId) {
    const auto title = blackwire::BuildConversationTitle("", "12345678-9abc-def0", "localhost:8000");
    EXPECT_EQ(title.toStdString(), "Conversation 12345678");
}

TEST(ConversationViewTest, SortsByLastActivityAscending) {
    blackwire::ConversationListItemView first;
    first.id = "1";
    first.title = "a";
    first.last_activity_at = "2026-02-14T10:00:00Z";

    blackwire::ConversationListItemView second;
    second.id = "2";
    second.title = "b";
    second.last_activity_at = "2026-02-14T12:00:00Z";

    std::vector<blackwire::ConversationListItemView> items = {first, second};

    blackwire::SortConversationItems(&items);
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[0].id.toStdString(), "1");
    EXPECT_EQ(items[1].id.toStdString(), "2");
}

TEST(ConversationViewTest, SortsAlphabeticallyWhenActivityMatches) {
    blackwire::ConversationListItemView first;
    first.id = "1";
    first.title = "zebra";
    first.last_activity_at = "2026-02-14T12:00:00Z";

    blackwire::ConversationListItemView second;
    second.id = "2";
    second.title = "alpha";
    second.last_activity_at = "2026-02-14T12:00:00Z";

    std::vector<blackwire::ConversationListItemView> items = {first, second};
    blackwire::SortConversationItems(&items);

    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[0].title.toStdString(), "alpha");
    EXPECT_EQ(items[1].title.toStdString(), "zebra");
}
