#include <gtest/gtest.h>

#include "blackwire/crypto/sodium_crypto_service.hpp"

TEST(CryptoServiceTest, EncryptDecryptRoundTrip) {
    blackwire::SodiumCryptoService crypto;
    const auto keys = crypto.GenerateDeviceKeys();

    const std::string plaintext = "hello blackwire";
    const auto ciphertext = crypto.EncryptForRecipient(keys.enc_x25519_public_b64, plaintext);
    const auto decrypted = crypto.DecryptWithPrivate(keys.enc_x25519_private_b64, ciphertext);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(CryptoServiceTest, SignVerifyRoundTrip) {
    blackwire::SodiumCryptoService crypto;
    const auto keys = crypto.GenerateDeviceKeys();

    const std::string payload = "signed-payload";
    const auto signature = crypto.SignDetached(keys.ik_ed25519_private_b64, payload);

    EXPECT_TRUE(crypto.VerifyDetached(keys.ik_ed25519_public_b64, payload, signature));
    EXPECT_FALSE(crypto.VerifyDetached(keys.ik_ed25519_public_b64, payload + "x", signature));
}
