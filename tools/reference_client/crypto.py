import base64

from nacl.public import PrivateKey, PublicKey, SealedBox
from nacl.signing import SigningKey


class CryptoError(ValueError):
    pass


def generate_device_bundle() -> dict[str, str]:
    signing_key = SigningKey.generate()
    verify_key = signing_key.verify_key

    enc_private = PrivateKey.generate()
    enc_public = enc_private.public_key

    return {
        "ik_ed25519_private": base64.b64encode(bytes(signing_key)).decode("ascii"),
        "ik_ed25519_public": base64.b64encode(bytes(verify_key)).decode("ascii"),
        "enc_x25519_private": base64.b64encode(bytes(enc_private)).decode("ascii"),
        "enc_x25519_public": base64.b64encode(bytes(enc_public)).decode("ascii"),
    }


def encrypt_for_recipient(recipient_public_b64: str, plaintext: str) -> str:
    try:
        recipient_pub = PublicKey(base64.b64decode(recipient_public_b64))
    except Exception as exc:  # pragma: no cover
        raise CryptoError("Invalid recipient public key") from exc

    box = SealedBox(recipient_pub)
    ciphertext = box.encrypt(plaintext.encode("utf-8"))
    return base64.b64encode(ciphertext).decode("ascii")


def decrypt_with_private(private_key_b64: str, ciphertext_b64: str) -> str:
    try:
        private_key = PrivateKey(base64.b64decode(private_key_b64))
        ciphertext = base64.b64decode(ciphertext_b64)
    except Exception as exc:  # pragma: no cover
        raise CryptoError("Invalid decryption payload") from exc

    box = SealedBox(private_key)
    plaintext = box.decrypt(ciphertext)
    return plaintext.decode("utf-8")
