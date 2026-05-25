#include <gtest/gtest.h>

#include <vector>

#include "blackwire/storage/client_state.hpp"
#include "blackwire/util/message_view.hpp"

TEST(MessageViewTest, GroupsConsecutiveMessagesFromSameSender) {
    blackwire::LocalMessage first;
    first.id = "1";
    first.sender_user_id = "self-id";
    first.created_at = "2026-02-14T10:00:00Z";
    first.plaintext = "hello";

    blackwire::LocalMessage second;
    second.id = "2";
    second.sender_user_id = "self-id";
    second.created_at = "2026-02-14T10:01:00Z";
    second.plaintext = "again";

    blackwire::LocalMessage third;
    third.id = "3";
    third.sender_user_id = "peer-id";
    third.created_at = "2026-02-14T10:02:00Z";
    third.plaintext = "reply";

    std::vector<blackwire::LocalMessage> input = {first, second, third};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "alice");

    ASSERT_EQ(views.size(), 3U);
    EXPECT_FALSE(views[0].grouped_with_previous);
    EXPECT_TRUE(views[1].grouped_with_previous);
    EXPECT_FALSE(views[2].grouped_with_previous);
    EXPECT_TRUE(views[0].outgoing);
    EXPECT_FALSE(views[2].outgoing);
}

TEST(MessageViewTest, UsesSenderAddressLabelForGroupMembersAndSeparatesSpeakers) {
    blackwire::LocalMessage first;
    first.id = "1";
    first.sender_user_id = "peer-a-id";
    first.sender_address = "alice@server-a.onion";
    first.created_at = "2026-02-14T10:00:00Z";
    first.plaintext = "hello";

    blackwire::LocalMessage second;
    second.id = "2";
    second.sender_user_id = "peer-b-id";
    second.sender_address = "bob@server-b.onion";
    second.created_at = "2026-02-14T10:01:00Z";
    second.plaintext = "hi";

    std::vector<blackwire::LocalMessage> input = {first, second};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "Member");

    ASSERT_EQ(views.size(), 2U);
    EXPECT_EQ(views[0].sender_label.toStdString(), "alice");
    EXPECT_EQ(views[1].sender_label.toStdString(), "bob");
    EXPECT_FALSE(views[1].grouped_with_previous);
}

TEST(MessageViewTest, DoesNotGroupSameSenderAfterLongGap) {
    blackwire::LocalMessage first;
    first.id = "1";
    first.sender_user_id = "peer-a-id";
    first.sender_address = "alice@server-a.onion";
    first.created_at = "2026-02-14T10:00:00Z";
    first.plaintext = "hello";

    blackwire::LocalMessage second;
    second.id = "2";
    second.sender_user_id = "peer-a-id";
    second.sender_address = "alice@server-a.onion";
    second.created_at = "2026-02-14T10:20:00Z";
    second.plaintext = "later";

    std::vector<blackwire::LocalMessage> input = {first, second};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "Member");

    ASSERT_EQ(views.size(), 2U);
    EXPECT_FALSE(views[0].grouped_with_previous);
    EXPECT_FALSE(views[1].grouped_with_previous);
}

TEST(MessageViewTest, DoesNotGroupSameUsernameFromDifferentAddresses) {
    blackwire::LocalMessage first;
    first.id = "1";
    first.sender_address = "alice@server-a.onion";
    first.created_at = "2026-02-14T10:00:00Z";
    first.plaintext = "hello";

    blackwire::LocalMessage second;
    second.id = "2";
    second.sender_address = "alice@server-b.onion";
    second.created_at = "2026-02-14T10:01:00Z";
    second.plaintext = "different alice";

    std::vector<blackwire::LocalMessage> input = {first, second};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "Member");

    ASSERT_EQ(views.size(), 2U);
    EXPECT_EQ(views[0].sender_label.toStdString(), "alice");
    EXPECT_EQ(views[1].sender_label.toStdString(), "alice");
    EXPECT_FALSE(views[1].grouped_with_previous);
}

TEST(MessageViewTest, FormatsInvalidTimestampWithFallback) {
    const auto display = blackwire::FormatThreadTimestamp("not-a-real-time");
    EXPECT_EQ(display.toStdString(), "not-a-real-time");
}

TEST(MessageViewTest, ExtractsLegacyPlaintextFromRenderedLine) {
    const auto plaintext = blackwire::ExtractLegacyPlaintext("[2026-02-14T10:02:00Z] Me: hello world");
    EXPECT_EQ(plaintext.toStdString(), "hello world");
}

TEST(MessageViewTest, OrdersThreadChronologicallySoLatestIsAtBottom) {
    blackwire::LocalMessage newest;
    newest.id = "3";
    newest.sender_user_id = "peer-id";
    newest.created_at = "2026-02-14T10:02:00Z";
    newest.plaintext = "latest";

    blackwire::LocalMessage oldest;
    oldest.id = "1";
    oldest.sender_user_id = "self-id";
    oldest.created_at = "2026-02-14T10:00:00Z";
    oldest.plaintext = "oldest";

    blackwire::LocalMessage middle;
    middle.id = "2";
    middle.sender_user_id = "peer-id";
    middle.created_at = "2026-02-14T10:01:00Z";
    middle.plaintext = "middle";

    std::vector<blackwire::LocalMessage> input = {newest, oldest, middle};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "alice");

    ASSERT_EQ(views.size(), 3U);
    EXPECT_EQ(views[0].body.toStdString(), "oldest");
    EXPECT_EQ(views[1].body.toStdString(), "middle");
    EXPECT_EQ(views[2].body.toStdString(), "latest");
}

TEST(MessageViewTest, UsesAttachmentRenderModeForBwfileMessages) {
    blackwire::LocalMessage attachment;
    attachment.id = "a1";
    attachment.sender_user_id = "self-id";
    attachment.created_at = "2026-03-01T10:00:00Z";
    attachment.plaintext = "bwfile://v1:eyJuYW1lIjoiZmlsZS50eHQiLCJkYXRhX2I2NCI6IlptOXYifQ==";

    std::vector<blackwire::LocalMessage> input = {attachment};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "peer");

    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views[0].render_mode.toStdString(), "attachment");
}

TEST(MessageViewTest, UsesMarkdownRenderModeForPlainMessages) {
    blackwire::LocalMessage plain;
    plain.id = "m1";
    plain.sender_user_id = "self-id";
    plain.created_at = "2026-03-01T10:00:00Z";
    plain.plaintext = "**hello** _world_";

    std::vector<blackwire::LocalMessage> input = {plain};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "peer");

    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views[0].render_mode.toStdString(), "markdown");
}

TEST(MessageViewTest, SurfacesAttachmentStatusAndRetryability) {
    blackwire::LocalMessage outgoing;
    outgoing.id = "m1";
    outgoing.sender_user_id = "self-id";
    outgoing.created_at = "2026-03-01T10:00:00Z";
    outgoing.plaintext = "bwfile://v1:eyJuYW1lIjoiYmlnLm1wNCIsImRhdGFfYjY0IjoiWm05diJ9";
    outgoing.attachment_status = "failed";
    outgoing.retry_payload = R"({"file_path":"C:\\tmp\\big.mp4"})";

    std::vector<blackwire::LocalMessage> input = {outgoing};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "peer");

    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views[0].attachment_status.toStdString(), "failed");
    EXPECT_TRUE(views[0].attachment_retryable);
}

TEST(MessageViewTest, TreatsSenderlessMessagesAsSystemMessages) {
    blackwire::LocalMessage system;
    system.id = "sys-1";
    system.created_at = "2026-03-01T10:00:00Z";
    system.plaintext = "alice started a call that lasted 2 minutes.";
    system.sender_user_id.clear();
    system.sender_address.clear();

    std::vector<blackwire::LocalMessage> input = {system};
    const auto views = blackwire::BuildThreadMessageViews(input, "self-id", "peer");

    ASSERT_EQ(views.size(), 1U);
    EXPECT_TRUE(views[0].system);
    EXPECT_EQ(views[0].render_mode.toStdString(), "system");
    EXPECT_TRUE(views[0].sender_label.isEmpty());
    EXPECT_FALSE(views[0].outgoing);
}
