# Blackwire

Blackwire v0.3 (Wave 1) is a security-first encrypted messaging platform with Tor-native federation.

## What is implemented

- FastAPI backend (`server/app`) with REST + WebSocket.
- PostgreSQL-first persistence using SQLAlchemy 2 and Alembic.
- Ciphertext-only message storage and forwarding.
- Password auth with JWT access tokens and rotating refresh tokens.
- Single active device model per account.
- Direct 1:1 conversations and group conversations.
- Local + federated (`username@onion`) DM routing.
- Home-server-only client routing (client talks only to its configured home server URL).
- Canonical user identity returned by server as `user_address` (`username@onion`).
- At-least-once delivery with websocket ack and offline queue.
- 7-day TTL expiry for undelivered queue entries.
- Signed server-to-server federation requests with TOFU key pinning.
- Federated message relay with durable outbox retry.
- Federated voice call signaling (direct and group call flows).
- Typing indicator contracts/events (`/api/v2`) with federation relay.
- Conversation read cursor contracts/events (`/api/v2`) with federation relay.
- Server version endpoint (`GET /api/v2/system/version`).
- Qt client markdown message rendering with raw HTML disabled.
- Qt client inline image rendering and click-to-play inline video dialog.
- Qt client attachment lifecycle UX (`queued`, `sending`, `success`, `failed`, retry).
- Qt client settings display of client/server version in `Settings -> My Account`.
- Qt client encrypted message cache with user privacy control toggle (`Settings → Data & Privacy`).
- Qt client external link click confirmation dialog for markdown links.
- Qt client Discord-style dark theme with flat design and inline avatars.
- Qt client friends list filtering (excludes group DMs from contacts sidebar).
- Qt client call message cache formatting (CALL icon/styling persists after conversation switches).
- Qt client call target stability (peer name from actual call target, not selected conversation).
- JSON deserialization hardening for state persistence (null-safe field loading).
- Optional Redis-backed rate-limiter mode (core works without Redis).
- Python reference client (`tools/reference_client`) with libsodium sealed-box encrypt/decrypt flow.
- Unit + integration tests under `server/tests`.

## Repository layout

```text
spec/
  api.md
  crypto.md
  protocol.md

client-cpp-gui/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  src/
  include/
  tests/
  scripts/

server/
  app/
  migrations/
  tests/
  pyproject.toml
  alembic.ini
  Dockerfile
  Makefile

tools/reference_client/
  cli.py
  crypto.py
  api.py
  state.py

infra/
  docker-compose.yml
  docker-compose.redis.yml
  example.env
```

## Quick start (Docker)

1. Review environment values:

```bash
cp infra/example.env infra/.env
```

Important:
- Randomize secrets before first startup:
  - `./infra/randomize-env-secrets.ps1`
- If `BLACKWIRE_TOR_ENABLED=true`, set `BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64` to a base64-encoded 32-byte key before startup.

2. Start Postgres + API server:

```bash
docker compose -f infra/docker-compose.yml up --build
```

3. API docs:

- `http://localhost:8000/docs`

4. Readiness:

- `http://localhost:8000/health/live`
- `http://localhost:8000/health/ready`

### Optional Redis profile

```bash
docker compose \
  -f infra/docker-compose.yml \
  -f infra/docker-compose.redis.yml \
  up --build
```

## Local development

```bash
cd server
python -m pip install -e .[dev]
alembic upgrade head
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

## Test and quality commands

```bash
cd server
pytest -q
ruff check app tests
mypy app
```

## Reference client usage

Run from repository root:

```bash
python -m tools.reference_client.cli register alice password123
python -m tools.reference_client.cli device-init laptop
python -m tools.reference_client.cli --state bob.json register bob password123
python -m tools.reference_client.cli --state bob.json device-init phone
python -m tools.reference_client.cli send bob "hello"
python -m tools.reference_client.cli --state bob.json open-ws
```

## C++ GUI client (Qt)

Build from repository root:

```bash
cd client-cpp-gui
./scripts/bootstrap-windows.ps1
./scripts/build.ps1 -Config Debug
./scripts/test.ps1 -Config Debug
```

Run desktop app:

```bash
./scripts/run.ps1 -Config Debug
```

Typical client flow is connecting to home server via IP/domain (no Tor client required).
If you choose to connect GUI directly to an `.onion` home server URL, a SOCKS5 proxy is needed.
Default proxy: `socks5h://127.0.0.1:9050`; override example (Tor Browser `9150`):

```powershell
$env:BLACKWIRE_TOR_SOCKS5_URL = "socks5h://127.0.0.1:9150"
./scripts/run.ps1 -Config Debug
```

Reset local client state/credentials:

```bash
./scripts/reset-state.ps1
```

Create a portable release bundle (Qt + MSVC runtime included):

```bash
./scripts/package-release.ps1
```

Run client smoke E2E (requires running server):

```bash
./scripts/smoke-e2e.ps1 -BaseUrl http://localhost:8000
```

## API surface

Primary API prefix: `/api/v2`  
Legacy compatibility API prefix: `/api/v1` (still supported in this wave)

Key `/api/v2` routes include:

- Auth and device lifecycle (`/auth/*`, `/devices/*`, `/users/*`, `/keys/*`)
- Presence (`POST /presence/set`, `POST /presence/resolve`)
- Conversations (`/conversations/dm`, `/conversations/group`, `/conversations/{id}/members/*`, `/conversations/{id}/messages`)
- Typing/read state:
  - `POST /conversations/{conversation_id}/typing`
  - `POST /conversations/{conversation_id}/read`
  - `GET /conversations/{conversation_id}/read`
- Messaging (`POST /messages/send`)
- Federation (`/federation/*`) including:
  - `POST /federation/conversations/typing`
  - `POST /federation/conversations/read`
- System:
  - `GET /system/version`
- WebSocket:
  - `GET /api/v2/ws` (requires bearer auth during websocket handshake)

Legacy `/api/v1` routes remain available for compatibility (see `spec/api.md`).

## Current constraints

- One active device per user.
- Sealed-box message encryption baseline remains; ratchet scaffolding exists but full protocol migration is phased.

## License

See `LICENSE.md`.
