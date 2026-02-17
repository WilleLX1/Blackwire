#include "blackwire/crypto/sodium_crypto_service.hpp"

#include <cstring>
#include <stdexcept>

#include <sodium.h>

namespace blackwire {

SodiumCryptoService::SodiumCryptoService() {
    if (sodium_init() < 0) {
        throw std::runtime_error("Unable to initialize libsodium");
    }
}

DeviceKeyMaterial SodiumCryptoService::GenerateDeviceKeys() {
    unsigned char sign_pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sign_sk[crypto_sign_SECRETKEYBYTES];
    if (crypto_sign_keypair(sign_pk, sign_sk) != 0) {
        throw std::runtime_error("Unable to generate Ed25519 keypair");
    }

    unsigned char box_pk[crypto_box_PUBLICKEYBYTES];
    unsigned char box_sk[crypto_box_SECRETKEYBYTES];
    if (crypto_box_keypair(box_pk, box_sk) != 0) {
        throw std::runtime_error("Unable to generate X25519 keypair");
    }

    DeviceKeyMaterial out;
    out.ik_ed25519_public_b64 = EncodeBase64(sign_pk, crypto_sign_PUBLICKEYBYTES);
    out.ik_ed25519_private_b64 = EncodeBase64(sign_sk, crypto_sign_SECRETKEYBYTES);
    out.enc_x25519_public_b64 = EncodeBase64(box_pk, crypto_box_PUBLICKEYBYTES);
    out.enc_x25519_private_b64 = EncodeBase64(box_sk, crypto_box_SECRETKEYBYTES);
    return out;
}

std::string SodiumCryptoService::EncryptForRecipient(
    const std::string& recipient_public_b64,
    const std::string& plaintext) {
    const auto recipient_key = DecodeBase64(recipient_public_b64);
    if (recipient_key.size() != crypto_box_PUBLICKEYBYTES) {
        throw std::runtime_error("Recipient public key has invalid length");
    }

    std::vector<unsigned char> ciphertext(plaintext.size() + crypto_box_SEALBYTES);
    if (crypto_box_seal(
            ciphertext.data(),
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            plaintext.size(),
            recipient_key.data()) != 0) {
        throw std::runtime_error("Unable to encrypt message");
    }

    return EncodeBase64(ciphertext.data(), ciphertext.size());
}

std::string SodiumCryptoService::DecryptWithPrivate(
    const std::string& private_key_b64,
    const std::string& ciphertext_b64) {
    const auto private_key = DecodeBase64(private_key_b64);
    if (private_key.size() != crypto_box_SECRETKEYBYTES) {
        throw std::runtime_error("Private key has invalid length");
    }

    unsigned char public_key[crypto_box_PUBLICKEYBYTES];
    if (crypto_scalarmult_base(public_key, private_key.data()) != 0) {
        throw std::runtime_error("Unable to derive public key from private key");
    }

    const auto ciphertext = DecodeBase64(ciphertext_b64);
    if (ciphertext.size() < crypto_box_SEALBYTES) {
        throw std::runtime_error("Ciphertext too short");
    }

    std::vector<unsigned char> plaintext(ciphertext.size() - crypto_box_SEALBYTES);
    if (crypto_box_seal_open(
            plaintext.data(),
            ciphertext.data(),
            ciphertext.size(),
            public_key,
            private_key.data()) != 0) {
        throw std::runtime_error("Unable to decrypt message");
    }

    return std::string(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
}

std::string SodiumCryptoService::EncodeBase64(const unsigned char* bytes, std::size_t length) {
    const std::size_t out_size = sodium_base64_ENCODED_LEN(length, sodium_base64_VARIANT_ORIGINAL);
    std::string out(out_size, '\0');
    sodium_bin2base64(out.data(), out.size(), bytes, length, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::vector<unsigned char> SodiumCryptoService::DecodeBase64(const std::string& value) {
    std::vector<unsigned char> out(value.size(), 0);
    std::size_t decoded = 0;
    if (sodium_base642bin(
            out.data(),
            out.size(),
            value.c_str(),
            value.size(),
            nullptr,
            &decoded,
            nullptr,
            sodium_base64_VARIANT_ORIGINAL) != 0) {
        throw std::runtime_error("Invalid base64 payload");
    }
    out.resize(decoded);
    return out;
}

}  // namespace blackwire
