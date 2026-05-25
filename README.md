# Blackwire

Blackwire v0.4 is a security-first encrypted messaging platform with Tor-native federation.

The checked-in Docker quick start follows `infra/example.env`, which ships with
`BLACKWIRE_TLS_ENABLED=false`, so the server comes up on plain HTTP unless you
override that setting in `infra/.env`. Fresh GUI client profiles default to
`http://localhost:8000` to match the stock Docker setup.

## What is implemented

- FastAPI backend (`server/app`) with REST + WebSocket.
- PostgreSQL-first persistence using SQLAlchemy 2 and Alembic.
- Ciphertext-only message storage and forwarding.
- Password auth with JWT access tokens and rotating refresh tokens.
- Legacy `/api/v1` single-active-device model plus `/api/v2` multi-device support per account.
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
- Container entrypoint support for self-signed TLS with persisted cert volume when enabled.
- Qt client markdown message rendering with raw HTML disabled.
- Qt client inline image rendering and click-to-play inline video dialog.
- Qt client attachment lifecycle UX (`queued`, `sending`, `success`, `failed`, `retry`).
- Qt client settings display of client/server version in `Settings -> My Account`.
- Qt client encrypted message cache with user privacy control toggle (`Settings -> Data & Privacy`).
- Qt client warning dialog for self-signed or otherwise untrusted TLS certificates.
- Qt client external link click confirmation dialog for markdown links.
- Qt client Discord-style dark theme with flat design and inline avatars.
- Qt client friends list filtering (excludes group DMs from contacts sidebar).
- Qt client call message cache formatting (CALL icon/styling persists after conversation switches).
- Qt client call target stability (peer name from actual call target, not selected conversation).
- JSON deserialization hardening for state persistence (null-safe field loading).
- Server startup rejection of known insecure JWT defaults outside dev.
- Per-account login rate limiting and password complexity enforcement.
- Optional Redis-backed rate-limiter mode (core works without Redis; startup warns outside dev).
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
- `POSTGRES_PASSWORD` still falls back to `blackwire` if unset in compose; set
  it explicitly in `infra/.env` for anything beyond throwaway local dev.
- `BLACKWIRE_TLS_ENABLED=false` in `infra/example.env`. Leave it false for
  plain HTTP local dev, or set `BLACKWIRE_TLS_ENABLED=true` in `infra/.env` to
  enable self-signed HTTPS.
- If `BLACKWIRE_TOR_ENABLED=true`, set `BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64`
  to a base64-encoded 32-byte key before startup.

2. Start Postgres + API server:

```bash
docker compose -f infra/docker-compose.yml up --build
```

3. API docs:

- With the shipped example env (TLS disabled): `http://localhost:8000/docs`
- If you enable TLS: `https://localhost:8000/docs` or `https://localhost:8443/docs`

4. Readiness:

- With the shipped example env (TLS disabled): `http://localhost:8000/health/live`
- With the shipped example env (TLS disabled): `http://localhost:8000/health/ready`
- If you enable TLS: `https://localhost:8000/health/live`
- If you enable TLS: `https://localhost:8000/health/ready`

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

The direct `uvicorn` command above serves HTTP. For local HTTPS, use the Docker
entrypoint path or provide Uvicorn `--ssl-keyfile` / `--ssl-certfile` flags
yourself.

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
python -m tools.reference_client.cli register alice "Password123!"
python -m tools.reference_client.cli device-init laptop
python -m tools.reference_client.cli --state bob.json register bob "Password123!"
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

Fresh profiles default to `http://localhost:8000`. To point an existing profile
at the local Docker server, run with `-BaseUrl http://localhost:8000` once or
change the home server URL in the login screen. If you enable TLS in `infra/.env`,
use `https://localhost:8000` instead.

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

For the stock Docker quick start (TLS disabled):

```bash
./scripts/smoke-e2e.ps1 -BaseUrl http://localhost:8000
```

If you enable self-signed TLS:

```bash
./scripts/smoke-e2e.ps1 -BaseUrl https://localhost:8000
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
  - `GET /api/v2/ws` (the C++ client uses bearer auth during the websocket handshake; the V2 server still contains a legacy `?access_token=` fallback)

Legacy `/api/v1` routes remain available for compatibility (see `spec/api.md`).

## Current constraints

- `/api/v1` exposes one active device per user for compatibility.
- `/api/v2` supports multiple active devices per user.
- Sealed-box message encryption baseline remains; ratchet scaffolding exists but full protocol migration is phased.

## License

See `LICENSE.md`.
