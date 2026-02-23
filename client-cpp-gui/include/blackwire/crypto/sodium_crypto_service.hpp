#pragma once

#include <vector>

#include "blackwire/interfaces/crypto_service.hpp"

namespace blackwire {

class SodiumCryptoService final : public ICryptoService {
public:
    SodiumCryptoService();

    DeviceKeyMaterial GenerateDeviceKeys() override;
    std::string EncryptForRecipient(
        const std::string& recipient_public_b64,
        const std::string& plaintext) override;
    std::string DecryptWithPrivate(
        const std::string& private_key_b64,
        const std::string& ciphertext_b64) override;
    std::string SignDetached(
        const std::string& ed25519_private_key_b64,
        const std::string& message) override;
    bool VerifyDetached(
        const std::string& ed25519_public_key_b64,
        const std::string& message,
        const std::string& signature_b64) override;
    std::string Sha256(const std::string& data) override;

private:
    static std::string EncodeBase64(const unsigned char* bytes, std::size_t length);
    static std::vector<unsigned char> DecodeBase64(const std::string& value);
};

}  // namespace blackwire
