# Blackwire C++ GUI Client

Qt 6 Widgets desktop client for Blackwire v0.1.

## Features

- Register/login/logout with refresh-token retry.
- Device key generation and registration (libsodium).
- Manual username-based DM creation.
- Ciphertext send using sealed boxes.
- Real-time receive/decrypt/ack over WebSocket.
- Friendly conversation list labels with preview + recency sorting.
- Inline status/error banners and settings dialog (copy ID/device ID/diagnostics).
- JSON non-secret state + Windows Credential Manager secret storage.
- Headless smoke mode for automated E2E verification.

## Build (Windows)

1. Bootstrap prerequisites and vcpkg:

```powershell
./scripts/bootstrap-windows.ps1
```

2. Configure and build:

```powershell
./scripts/build.ps1 -Config Debug
```

3. Run tests:

```powershell
./scripts/test.ps1 -Config Debug
```

4. Run app:

```powershell
./scripts/run.ps1 -Config Debug
```

Run multiple clients on one machine with isolated local state:

```powershell
./scripts/run.ps1 -Config Debug -Profile alice
./scripts/run.ps1 -Config Debug -Profile bob
```

Launch detached from one terminal:

```powershell
./scripts/run.ps1 -Config Debug -Profile alice -Detached
./scripts/run.ps1 -Config Debug -Profile bob -Detached
```

Equivalent direct executable flag:

```powershell
blackwire_client.exe --profile=alice
```

## Portable Release Bundle (no manual VC++ install)

Create a distributable zip with Qt + MSVC runtime DLLs:

```powershell
./scripts/package-release.ps1
```

Send `dist/blackwire-client-windows-x64.zip` to the target machine, extract it, and run `blackwire_client.exe`.

## Reset local state

Clear local state and all `blackwire` credentials:

```powershell
./scripts/reset-state.ps1
```

Reset one profile only:

```powershell
./scripts/reset-state.ps1 -Profile alice
```

## Smoke test

Assuming server is running at `http://localhost:8000`:

```powershell
./scripts/smoke-e2e.ps1 -BaseUrl http://localhost:8000
```
