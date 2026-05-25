import hashlib
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from functools import lru_cache
from typing import Any

import jwt
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ed25519

from app.config import get_settings


class TokenV2Error(ValueError):
    pass


@dataclass(slots=True)
class DeviceTokenPairV2:
    access_token: str
    access_expires_in: int
    refresh_token: str
    refresh_expires_in: int
    refresh_token_id: str
    refresh_expires_at: datetime


def _now_utc() -> datetime:
    return datetime.now(UTC)


def _as_pem_bytes(value: str) -> bytes:
    normalized = value.strip()
    if not normalized:
        return b""
    return normalized.encode("utf-8")


@lru_cache
def _signing_private_key() -> ed25519.Ed25519PrivateKey:
    settings = get_settings()
    private_pem = _as_pem_bytes(settings.jwt_private_key_pem)
    if private_pem:
        key = serialization.load_pem_private_key(private_pem, password=None)
        if not isinstance(key, ed25519.Ed25519PrivateKey):
            raise RuntimeError("BLACKWIRE_JWT_PRIVATE_KEY_PEM must be an Ed25519 key")
        return key

    seed = hashlib.sha256(f"{settings.jwt_secret_key}|blackwire-v2-jwt-seed".encode()).digest()
    return ed25519.Ed25519PrivateKey.from_private_bytes(seed[:32])


@lru_cache
def _verify_public_key() -> ed25519.Ed25519PublicKey:
    settings = get_settings()
    public_pem = _as_pem_bytes(settings.jwt_public_key_pem)
    if public_pem:
        key = serialization.load_pem_public_key(public_pem)
        if not isinstance(key, ed25519.Ed25519PublicKey):
            raise RuntimeError("BLACKWIRE_JWT_PUBLIC_KEY_PEM must be an Ed25519 key")
        return key
    return _signing_private_key().public_key()


def reset_v2_token_cache() -> None:
    _signing_private_key.cache_clear()
    _verify_public_key.cache_clear()


def _signing_private_key_pem() -> bytes:
    return _signing_private_key().private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )


def _verify_public_key_pem() -> bytes:
    return _verify_public_key().public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )


def _encode(payload: dict[str, Any]) -> str:
    token = jwt.encode(payload, _signing_private_key_pem(), algorithm="EdDSA")
    if isinstance(token, bytes):
        return token.decode("utf-8")
    return token


def create_bootstrap_token(user_id: str, username: str) -> tuple[str, int]:
    settings = get_settings()
    now = _now_utc()
    expiry = now + timedelta(seconds=max(30, settings.v2_bootstrap_token_seconds))
    payload = {
        "sub": user_id,
        "username": username,
        "did": "",
        "type": "bootstrap",
        "jti": str(uuid.uuid4()),
        "iat": int(now.timestamp()),
        "exp": int(expiry.timestamp()),
    }
    ttl = int((expiry - now).total_seconds())
    return _encode(payload), ttl


def create_access_token(user_id: str, username: str, device_uid: str) -> tuple[str, int]:
    settings = get_settings()
    now = _now_utc()
    expiry = now + timedelta(minutes=max(1, settings.v2_access_token_minutes))
    payload = {
        "sub": user_id,
        "username": username,
        "did": device_uid,
        "type": "access",
        "jti": str(uuid.uuid4()),
        "iat": int(now.timestamp()),
        "exp": int(expiry.timestamp()),
    }
    return _encode(payload), int((expiry - now).total_seconds())


def create_refresh_token(user_id: str, device_uid: str, token_id: str | None = None) -> tuple[str, str, datetime, int]:
    settings = get_settings()
    now = _now_utc()
    expiry = now + timedelta(days=max(1, settings.v2_refresh_token_days))
    refresh_id = token_id or str(uuid.uuid4())
    payload = {
        "sub": user_id,
        "username": "",
        "did": device_uid,
        "type": "refresh",
        "rid": refresh_id,
        "jti": str(uuid.uuid4()),
        "iat": int(now.timestamp()),
        "exp": int(expiry.timestamp()),
    }
    token = _encode(payload)
    return token, refresh_id, expiry, int((expiry - now).total_seconds())


def decode_token(token: str, expected_type: str | None = None) -> dict[str, Any]:
    try:
        payload = jwt.decode(
            token,
            _verify_public_key_pem(),
            algorithms=["EdDSA"],
            options={"require": ["exp", "iat", "sub", "did", "type", "jti"]},
        )
    except jwt.ExpiredSignatureError as exc:
        raise TokenV2Error("Token expired") from exc
    except jwt.InvalidTokenError as exc:
        raise TokenV2Error("Invalid token") from exc

    token_type = payload.get("type")
    if expected_type and token_type != expected_type:
        raise TokenV2Error(f"Expected {expected_type} token")
    return payload


def build_device_token_pair(user_id: str, username: str, device_uid: str) -> DeviceTokenPairV2:
    access_token, access_ttl = create_access_token(user_id=user_id, username=username, device_uid=device_uid)
    refresh_token, refresh_id, refresh_expiry, refresh_ttl = create_refresh_token(
        user_id=user_id, device_uid=device_uid
    )
    return DeviceTokenPairV2(
        access_token=access_token,
        access_expires_in=access_ttl,
        refresh_token=refresh_token,
        refresh_expires_in=refresh_ttl,
        refresh_token_id=refresh_id,
        refresh_expires_at=refresh_expiry,
    )


def hash_refresh_token(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()
