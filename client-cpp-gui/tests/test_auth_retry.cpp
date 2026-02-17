#include <gtest/gtest.h>

#include "blackwire/interfaces/api_client.hpp"
#include "blackwire/util/auth_retry.hpp"

TEST(AuthRetryTest, RetriesExactlyOnceOnUnauthorized) {
    int attempts = 0;
    bool refreshed = false;

    const auto op = [&]() {
        attempts += 1;
        if (attempts == 1) {
            throw blackwire::ApiException(401, "unauthorized");
        }
        return 42;
    };

    const auto refresh = [&]() {
        refreshed = true;
    };

    const auto result = blackwire::CallWithAuthRetryOnce<int>(op, refresh);
    EXPECT_TRUE(refreshed);
    EXPECT_EQ(attempts, 2);
    EXPECT_EQ(result, 42);
}

TEST(AuthRetryTest, RetriesOnRateLimitBeforeFailing) {
    int attempts = 0;
    bool refreshed = false;

    const auto op = [&]() {
        attempts += 1;
        if (attempts < 3) {
            throw blackwire::ApiException(429, "rate limit");
        }
        return 7;
    };

    const auto refresh = [&]() {
        refreshed = true;
    };

    const auto result = blackwire::CallWithAuthRetryOnce<int>(op, refresh);
    EXPECT_FALSE(refreshed);
    EXPECT_EQ(attempts, 3);
    EXPECT_EQ(result, 7);
}
