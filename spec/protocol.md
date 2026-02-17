# Blackwire v0.1 Protocol Behavior

## Identity Model

- Canonical user identity: `username@onion`.
- Users authenticate only on their home server.
- Client transport is home-server-only: client talks only to configured home server URL.
- Server returns canonical `user_address` for identity display and copy.
- One active device is exposed per user in v0.1.

## Conversation Model

- `local` conversations: unique sorted local user pair.
- `remote` conversations: unique `(local_user_id, peer_address)`.
- Conversation responses always include `peer_address`, `peer_username`, and `peer_server_onion`.

## Delivery Semantics

- Local delivery: at-least-once via websocket + ack + offline queue.
- Cross-server delivery: at-least-once to remote federation ingress, then remote local at-least-once.
- Duplicate sends remain idempotent on `(sender_device_id, client_message_id)`.

## Federation Security

- Federation writes are signed with Ed25519.
- Canonical signature string:
  `METHOD\nPATH\nSHA256(BODY)\nTIMESTAMP\nNONCE\nSENDER_ONION`
- Timestamp skew is bounded by `BLACKWIRE_FEDERATION_REQUEST_SKEW_SECONDS`.
- Nonce replay is prevented per `(peer_onion, nonce)` with TTL.
- Trust model is TOFU:
  - first successful discovery stores peer signing key,
  - subsequent key mismatch marks peer as `key_conflict` and rejects traffic.

## Tor Transport

- Tor runs as a sidecar container per server deployment.
- Hidden service hostname defines canonical local server onion identity.
- Federation HTTP traffic uses Tor SOCKS5 (`socks5h`) when enabled.
- Clients do not need Tor when connecting to home server over IP/domain.
- Optional client connection to onion home-server URLs is best-effort and may require a local SOCKS proxy.

## Voice Call Federation

- Local-local calls keep websocket signaling/audio relay behavior.
- Local-remote calls proxy call control/audio through federation endpoints.
- Client websocket event names remain stable with added address fields.

## Queue Lifecycle

- Message queue statuses: `pending`, `delivered`, `expired`.
- Federation outbox statuses: `pending`, `sent` with retry/backoff metadata.

## Remaining v0.1 Constraints

- One active device per user.
- Sealed-box message encryption (no Noise session ratchet yet).
- Group messaging and group voice are out of scope.
