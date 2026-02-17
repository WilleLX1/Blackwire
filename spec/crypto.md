# Blackwire v0.1 Crypto Spec

## Scope

v0.1 uses client-side sealed-box encryption for direct messages. The server stores and forwards ciphertext only.

## Algorithm

- Algorithm id: `libsodium-sealedbox-v1`
- Primitive: libsodium sealed boxes (`crypto_box_seal`)
- Key type: X25519 keypair per device

## Device Keys

Each device publishes:
- `ik_ed25519_pub` (identity signing key, currently for metadata identity only)
- `enc_x25519_pub` (encryption public key)

Private keys remain local on the client.

## Envelope

```json
{
  "version": 1,
  "alg": "libsodium-sealedbox-v1",
  "recipient_device_id": "uuid",
  "ciphertext_b64": "base64",
  "aad_b64": null,
  "client_message_id": "uuid"
}
```

Server validation in v0.1:
- `alg` is exactly `libsodium-sealedbox-v1`.
- Base64 format checks for ciphertext/AAD.
- Ciphertext and AAD byte-size caps.
- Recipient device must match active recipient device.

## Security Notes

- No plaintext message fields are stored server-side.
- Server does not decrypt, inspect, or transform ciphertext.
- Delivery is at-least-once; clients must deduplicate by message id.
- v0.1 is not a full Noise/Double Ratchet deployment.
