from __future__ import annotations

from urllib.parse import urlsplit

from app.config import Settings
from app.services.server_identity import get_server_onion


def normalize_server_authority(value: str) -> str:
    normalized = value.strip().lower()
    if normalized.startswith("http://"):
        normalized = normalized[7:]
    if normalized.startswith("https://"):
        normalized = normalized[8:]
    return normalized.rstrip("/")


def _host_port(authority: str) -> tuple[str, int | None]:
    normalized = normalize_server_authority(authority)
    if not normalized:
        return "", None
    if normalized.count(":") >= 2 and not normalized.startswith("["):
        normalized = f"[{normalized}]"
    parsed = urlsplit(f"http://{normalized}")
    host = (parsed.hostname or normalized).strip().lower()
    try:
        port = parsed.port
    except ValueError:
        return host, None
    return host, port


def authority_matches(left: str, right: str) -> bool:
    left_norm = normalize_server_authority(left)
    right_norm = normalize_server_authority(right)
    if not left_norm or not right_norm:
        return False
    if left_norm == right_norm:
        return True
    left_host, left_port = _host_port(left_norm)
    right_host, right_port = _host_port(right_norm)
    if not left_host or not right_host:
        return False
    if left_host != right_host:
        return False
    return left_port is None or right_port is None or left_port == right_port


def local_server_authorities(settings: Settings) -> set[str]:
    authorities = {
        normalize_server_authority(get_server_onion()),
        normalize_server_authority(settings.federation_server_onion),
        "localhost",
        "127.0.0.1",
        "[::1]",
    }
    configured = settings.local_server_aliases.strip()
    if configured:
        for item in configured.split(","):
            alias = normalize_server_authority(item)
            if alias:
                authorities.add(alias)
    return {value for value in authorities if value}


def is_local_server_authority(
    authority: str,
    settings: Settings,
    additional_aliases: set[str] | None = None,
) -> bool:
    target = normalize_server_authority(authority)
    if not target:
        return False
    candidate_authorities = local_server_authorities(settings)
    if additional_aliases is not None:
        for alias in additional_aliases:
            normalized = normalize_server_authority(alias)
            if normalized:
                candidate_authorities.add(normalized)
    for local_authority in candidate_authorities:
        if authority_matches(target, local_authority):
            return True
    return False
