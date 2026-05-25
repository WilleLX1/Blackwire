# Blackwire API Spec (`v2` Primary, `v1` Compatible)

## Versioning

- Primary client integration surface: `/api/v2`
- Legacy compatibility surface: `/api/v1` (still operational in `v0.4`)

---

## `/api/v2` Core

### Authentication and Identity

- `POST /api/v2/auth/register`
- `POST /api/v2/auth/login`
- `POST /api/v2/auth/refresh`
- `POST /api/v2/auth/logout`
- `POST /api/v2/auth/bind-device`
- `GET /api/v2/me`

### Devices and Keys

- `POST /api/v2/devices/register`
- `GET /api/v2/devices`
- `POST /api/v2/devices/{device_uid}/revoke`
- `GET /api/v2/users/resolve-devices?peer_address=...`
- `GET /api/v2/users/resolve-prekeys?peer_address=...`
- `POST /api/v2/keys/prekeys/upload`

### Presence

- `POST /api/v2/presence/set`
- `POST /api/v2/presence/resolve`

### Conversations and Messages

- `POST /api/v2/conversations/dm`
- `POST /api/v2/conversations/group`
- `GET /api/v2/conversations`
- `GET /api/v2/conversations/{conversation_id}/members`
- `POST /api/v2/conversations/{conversation_id}/members/invite`
- `POST /api/v2/conversations/{conversation_id}/members/{member_address}/remove`
- `POST /api/v2/conversations/{conversation_id}/invites/accept`
- `POST /api/v2/conversations/{conversation_id}/leave`
- `POST /api/v2/conversations/{conversation_id}/rename`
- `GET /api/v2/conversations/{conversation_id}/recipients`
- `GET /api/v2/conversations/{conversation_id}/messages`
- `POST /api/v2/messages/send`

---

## `v0.3b` Additions (`/api/v2`)

### Typing Indicator Write Contract

`POST /api/v2/conversations/{conversation_id}/typing`

Request:

```json
{
  "state": "on",
  "client_ts_ms": 1700000000000
}
```

Response:

```json
{
  "ok": true,
  "expires_in_ms": 6000
}
```

Rules:

1. `state` is `on|off`.
2. Typing is ephemeral and not persisted.
3. Sender is excluded from fanout.

### Read Cursor Write and Query Contracts

`POST /api/v2/conversations/{conversation_id}/read`

Request:

```json
{
  "last_read_message_id": "message-id",
  "last_read_sent_at_ms": 1700000000123
}
```

Response:

```json
{
  "conversation_id": "conversation-id",
  "reader_user_address": "alice@local.invalid",
  "last_read_message_id": "message-id",
  "last_read_sent_at_ms": 1700000000123,
  "updated_at": "2026-03-01T12:00:00.000000+00:00"
}
```

`GET /api/v2/conversations/{conversation_id}/read`

Response:

```json
{
  "conversation_id": "conversation-id",
  "cursors": [
    {
      "user_address": "alice@local.invalid",
      "last_read_message_id": "message-id",
      "last_read_sent_at_ms": 1700000000123,
      "updated_at": "2026-03-01T12:00:00.000000+00:00"
    }
  ]
}
```

Rules:

1. Cursor is persisted per `(conversation_id, user_id)`.
2. Updates are monotonic; stale regressions are treated as no-op.

### System Version Contract

`GET /api/v2/system/version`

Response:

```json
{
  "server_version": "0.3.0",
  "api_version": "v2",
  "git_commit": "abc1234",
  "build_timestamp": "2026-03-01T00:00:00Z"
}
```

---

## `/api/v2/ws` Events

### Client-to-server events

- `message.ack`
- `call.offer`
- `call.accept`
- `call.reject`
- `call.end`
- `call.audio` (legacy WS audio mode when enabled)
- `call.webrtc.offer`
- `call.webrtc.answer`
- `call.webrtc.ice`

Typing and read writes are REST-only (`POST /typing`, `POST /read`), not websocket writes.

### Server-to-client events

- `message.new`
- `conversation.group.renamed`
- `conversation.typing`
- `conversation.read`
- Voice and group call events (`call.*`, `call.group.*`)

`conversation.typing` payload:

```json
{
  "type": "conversation.typing",
  "conversation_id": "conversation-id",
  "from_user_address": "bob@local.invalid",
  "state": "on",
  "expires_in_ms": 6000,
  "sent_at": "2026-03-01T12:00:00.000000+00:00"
}
```

`conversation.read` payload:

```json
{
  "type": "conversation.read",
  "conversation_id": "conversation-id",
  "reader_user_address": "bob@local.invalid",
  "last_read_message_id": "message-id",
  "last_read_sent_at_ms": 1700000000123,
  "updated_at": "2026-03-01T12:00:00.000000+00:00"
}
```

---

## `/api/v2/federation`

### Well-known and lookup

- `GET /api/v2/federation/well-known`
- `GET /api/v2/federation/users/{username}/devices`
- `GET /api/v2/federation/users/{username}/prekeys`
- `GET /api/v2/federation/groups/{group_uid}/snapshot`

### Signed federation write endpoints

Required headers:

- `X-BW-Fed-Server`
- `X-BW-Fed-Timestamp`
- `X-BW-Fed-Nonce`
- `X-BW-Fed-Signature`

Write endpoints include:

- `POST /api/v2/federation/messages/relay`
- `POST /api/v2/federation/groups/events`
- `POST /api/v2/federation/groups/invites/accept`
- `POST /api/v2/federation/conversations/typing`
- `POST /api/v2/federation/conversations/read`
- group-call and call signaling relay routes (`/group-calls/*`, `/calls/webrtc-*`)

---

## `/api/v1` Compatibility Notes

`/api/v1` remains functional in this wave for compatibility with older clients.  
It retains the existing auth/device/conversation/message/ws/federation routes and semantics while `/api/v2` is the primary integration surface.
