import base64
import hashlib
import re
import time
from pathlib import Path

from nacl import encoding, signing

from app.config import Settings

_server_onion: str = "local.invalid"
_signing_key: signing.SigningKey | None = None
_signing_public_key_b64: str = ""
_server_onion_source: str = "default"
_onion_authority_pattern = re.compile(r"^[a-z2-7]{16,56}\.onion$")


def _normalize_onion(authority: str) -> str:
    value = authority.strip().lower()
    if value.startswith("http://"):
        value = value[7:]
    if value.startswith("https://"):
        value = value[8:]
    if value.endswith("/"):
        value = value[:-1]
    return value


def _load_hidden_service_hostname(path: str) -> str | None:
    try:
        hostname = Path(path).read_text(encoding="utf-8").strip()
        normalized = _normalize_onion(hostname)
        return normalized or None
    except OSError:
        return None


def _is_onion_authority(value: str) -> bool:
    return bool(_onion_authority_pattern.fullmatch(value.strip().lower()))


def _build_signing_key(settings: Settings, server_onion: str) -> signing.SigningKey:
    if settings.tor_enabled:
        tor_key = _load_tor_hidden_service_signing_key(settings, server_onion)
        if tor_key is not None:
            return tor_key

    configured = settings.federation_signing_private_key_b64.strip()
    if configured:
        raw = base64.b64decode(configured.encode("utf-8"), validate=True)
        return signing.SigningKey(raw)

    if settings.tor_enabled:
        raise RuntimeError("Tor is enabled but federation signing key could not be derived from hidden-service key")

    seed = hashlib.sha256(
        f"{settings.jwt_secret_key}|{server_onion}|blackwire-federation-signing-seed".encode()
    ).digest()
    return signing.SigningKey(seed)


def _onion_v3_public_key(authority: str) -> bytes | None:
    value = authority.strip().lower()
    if value.endswith(".onion"):
        value = value[:-6]
    if len(value) != 56:
        return None
    try:
        decoded = base64.b32decode(value.upper())
    except Exception:
        return None
    if len(decoded) != 35:
        return None
    return decoded[:32]


def _load_tor_hidden_service_signing_key(settings: Settings, server_onion: str) -> signing.SigningKey | None:
    try:
        raw = Path(settings.tor_hs_ed25519_secret_key_file).read_bytes()
    except OSError:
        return None

    onion_pub = _onion_v3_public_key(server_onion)
    candidates: list[bytes] = []
    if len(raw) >= 64:
        candidates.append(raw[-64:-32])
        candidates.append(raw[-32:])
    if len(raw) >= 96:
        candidates.append(raw[-96:-64])
    if len(raw) >= 32:
        candidates.append(raw[:32])

    seen: set[bytes] = set()
    for candidate in candidates:
        if len(candidate) != 32 or candidate in seen:
            continue
        seen.add(candidate)
        try:
            key = signing.SigningKey(candidate)
        except Exception:
            continue
        if onion_pub is None or key.verify_key.encode() == onion_pub:
            return key
    return None


def _wait_for_hidden_service_hostname(path: str, wait_seconds: int) -> str | None:
    if wait_seconds <= 0:
        return _load_hidden_service_hostname(path)

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        value = _load_hidden_service_hostname(path)
        if value:
            return value
        time.sleep(0.5)
    return _load_hidden_service_hostname(path)


def initialize_server_identity(settings: Settings) -> None:
    global _server_onion, _signing_key, _signing_public_key_b64, _server_onion_source

    configured_onion = _normalize_onion(settings.federation_server_onion)
    if settings.tor_enabled:
        hidden_service_onion = _wait_for_hidden_service_hostname(
            settings.tor_hs_hostname_file,
            settings.tor_hs_hostname_wait_seconds,
        )
        if hidden_service_onion is None or not _is_onion_authority(hidden_service_onion):
            raise RuntimeError(
                "Tor is enabled but a valid hidden-service hostname was not found. "
                f"Expected *.onion at {settings.tor_hs_hostname_file}"
            )
        _server_onion = hidden_service_onion
        _server_onion_source = "tor_hidden_service"
    elif configured_onion:
        _server_onion = configured_onion
        _server_onion_source = "configured_fallback"
    else:
        _server_onion = "local.invalid"
        _server_onion_source = "default"

    _signing_key = _build_signing_key(settings, _server_onion)
    _signing_public_key_b64 = _signing_key.verify_key.encode(encoder=encoding.Base64Encoder).decode("utf-8")


def get_server_onion() -> str:
    return _server_onion


def get_server_onion_source() -> str:
    return _server_onion_source


def server_address_for_username(username: str) -> str:
    return f"{username.strip().lower()}@{_server_onion}"


def get_federation_signing_key() -> signing.SigningKey:
    if _signing_key is None:
        raise RuntimeError("Server identity not initialized")
    return _signing_key


def get_federation_signing_public_key_b64() -> str:
    return _signing_public_key_b64
