#include <gtest/gtest.h>

#include "blackwire/storage/in_memory_secret_store.hpp"

TEST(SecretStoreTest, SetGetDeleteContract) {
    blackwire::InMemorySecretStore store;

    std::string error;
    EXPECT_TRUE(store.SetSecret("key", "value", &error));
    EXPECT_TRUE(error.empty());

    const auto value = store.GetSecret("key", &error);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "value");

    EXPECT_TRUE(store.DeleteSecret("key", &error));
    EXPECT_FALSE(store.GetSecret("key", &error).has_value());
}

TEST(SecretStoreTest, DeleteSecretsMatchingContract) {
    blackwire::InMemorySecretStore store;
    std::string error;

    EXPECT_TRUE(store.SetSecret("blackwire:one", "value-1", &error));
    EXPECT_TRUE(store.SetSecret("blackwire:two", "value-2", &error));
    EXPECT_TRUE(store.SetSecret("other:key", "value-3", &error));

    EXPECT_TRUE(store.DeleteSecretsMatching("blackwire", &error));
    EXPECT_FALSE(store.GetSecret("blackwire:one", &error).has_value());
    EXPECT_FALSE(store.GetSecret("blackwire:two", &error).has_value());
    EXPECT_TRUE(store.GetSecret("other:key", &error).has_value());
}
