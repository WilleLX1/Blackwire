# Blackwire v0.1 API Spec

Base URL path prefix: `/api/v1`

## Authentication

### `POST /auth/register`
Registers a local user and returns auth tokens.
`user` payload includes:
- `id`
- `username`
- `created_at`
- `user_address` (canonical `username@onion`)
- `home_server_onion`

### `POST /auth/login`
Authenticates and issues access/refresh tokens.
Returns the same `user` fields listed above.

### `POST /auth/refresh`
Rotates refresh token and returns a new pair.
Returns the same `user` fields listed above.

### `POST /auth/logout`
Revokes a refresh token.

## Identity and Devices

### `GET /me`
Returns current authenticated user.
Response includes:
- `id`
- `username`
- `created_at`
- `user_address`
- `home_server_onion`

### `POST /devices/register`
Registers a new device and marks it active.

### `GET /users/{username}/device`
Legacy local-only device lookup.

### `GET /users/resolve-device?peer_address=username@onion`
Resolves local or federated active recipient device for a canonical peer address.
Client always calls this on its home server; remote lookup is server-to-server federation.

## Conversations

### `POST /conversations/dm`
Creates/returns a DM conversation.

Request supports:
- `peer_address` (`username@onion`) for canonical local/remote routing.
- `peer_username` as local-only backward-compatible alias.

Response includes:
- `id`
- `kind` (`local` or `remote`)
- `user_a_id`, `user_b_id` (local conversations)
- `local_user_id` (remote conversations)
- `created_at`
- `peer_username`
- `peer_server_onion`
- `peer_address`

### `GET /conversations`
Lists local and federated conversations with the same fields above.

### `GET /conversations/{conversation_id}/messages`
Lists stored ciphertext envelopes for a conversation.

## Messaging

### `POST /messages/send`
Sends a ciphertext envelope to the target device.

Response message fields include:
- `sender_user_id` (nullable for federated relayed messages)
- `sender_address` (canonical sender identity)
- `sender_device_id`
- `envelope_json`

## WebSocket

### `GET /ws`
Authenticated websocket for message delivery and voice signaling.
Requires `Authorization: Bearer <access_token>` during websocket handshake.

Server `message.new` payload includes:
- `message.sender_user_id`
- `message.sender_address`

Server call events keep existing names and add address fields:
- `call.incoming.from_user_address`
- `call.ringing.peer_user_address`
- `call.accepted.peer_user_address`
- `call.audio.from_user_address`

Client events remain:
- `message.ack`
- `call.offer`
- `call.accept`
- `call.reject`
- `call.end`
- `call.audio`

## Federation (`/federation`)

### `GET /federation/well-known`
Returns:
- `server_onion`
- `federation_version`
- `signing_public_key`

### `GET /federation/users/{username}/device`
Returns local user active device and canonical `peer_address` for remote servers.

### Signed Federation Write Endpoints
Required headers:
- `X-BW-Fed-Server`
- `X-BW-Fed-Timestamp`
- `X-BW-Fed-Nonce`
- `X-BW-Fed-Signature`

Canonical signature string:
`METHOD\nPATH\nSHA256(BODY)\nTIMESTAMP\nNONCE\nSENDER_ONION`

Write/control endpoints:
- `POST /federation/messages/relay`
- `POST /federation/calls/offer`
- `POST /federation/calls/accept`
- `POST /federation/calls/reject`
- `POST /federation/calls/end`
- `POST /federation/calls/audio`

## Health and Metrics

- `GET /health/live`
- `GET /health/ready`
- `GET /api/v1/metrics` (requires bearer auth)
