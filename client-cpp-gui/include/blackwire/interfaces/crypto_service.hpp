#pragma once

#include <string>

namespace blackwire {

struct DeviceKeyMaterial {
    std::string ik_ed25519_private_b64;
    std::string ik_ed25519_public_b64;
    std::string enc_x25519_private_b64;
    std::string enc_x25519_public_b64;
};

class ICryptoService {
public:
    virtual ~ICryptoService() = default;

    virtual DeviceKeyMaterial GenerateDeviceKeys() = 0;
    virtual std::string EncryptForRecipient(
        const std::string& recipient_public_b64,
        const std::string& plaintext) = 0;
    virtual std::string DecryptWithPrivate(
        const std::string& private_key_b64,
        const std::string& ciphertext_b64) = 0;
};

}  // namespace blackwire
