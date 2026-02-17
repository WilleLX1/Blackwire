#include <gtest/gtest.h>

#include "blackwire/util/diagnostics.hpp"

TEST(DiagnosticsTest, RedactsSensitiveKeys) {
    const QString source = "access_token=abc refresh_token=xyz enc_private=123 ik_private=456";
    const QString sanitized = blackwire::SanitizeDiagnosticsText(source);

    EXPECT_FALSE(sanitized.contains("access_token", Qt::CaseInsensitive));
    EXPECT_FALSE(sanitized.contains("refresh_token", Qt::CaseInsensitive));
    EXPECT_FALSE(sanitized.contains("enc_private", Qt::CaseInsensitive));
    EXPECT_FALSE(sanitized.contains("ik_private", Qt::CaseInsensitive));
    EXPECT_GE(sanitized.count("[redacted]"), 4);
}
