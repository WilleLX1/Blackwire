#include <gtest/gtest.h>

#include "blackwire/util/peer_address.hpp"

TEST(PeerAddressTest, ParsesUsernameOnly) {
    const auto parsed = blackwire::ParsePeerAddress("alice");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->username.toStdString(), "alice");
    EXPECT_FALSE(parsed->has_server);
}

TEST(PeerAddressTest, ParsesUsernameAndServer) {
    const auto parsed = blackwire::ParsePeerAddress("bob@192.168.1.11:8000");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->username.toStdString(), "bob");
    EXPECT_TRUE(parsed->has_server);
    EXPECT_EQ(parsed->server_authority.toStdString(), "192.168.1.11:8000");
}

TEST(PeerAddressTest, ParsesOnionAddress) {
    const auto parsed = blackwire::ParsePeerAddress("Bob@ExamplePeer.onion");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->username.toStdString(), "bob");
    EXPECT_EQ(parsed->server_authority.toStdString(), "examplepeer.onion");
}

TEST(PeerAddressTest, RejectsInvalid) {
    EXPECT_FALSE(blackwire::ParsePeerAddress("@example.com").has_value());
    EXPECT_FALSE(blackwire::ParsePeerAddress("alice@").has_value());
    EXPECT_FALSE(blackwire::ParsePeerAddress("alice@@host").has_value());
}

TEST(PeerAddressTest, MatchesServerAuthority) {
    EXPECT_TRUE(blackwire::ServerAuthorityMatchesBaseUrl("localhost:8000", "http://localhost:8000"));
    EXPECT_FALSE(blackwire::ServerAuthorityMatchesBaseUrl("localhost:9000", "http://localhost:8000"));
}
