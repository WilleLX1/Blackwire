import hashlib
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any

import jwt

from app.config import get_settings


class TokenError(ValueError):
    pass


@dataclass(slots=True)
class TokenPair:
    access_token: str
    access_expires_in: int
    refresh_token: str
    refresh_expires_in: int
    refresh_token_id: str
    refresh_expires_at: datetime


def _now_utc() -> datetime:
    return datetime.now(UTC)


def _encode(payload: dict[str, Any]) -> str:
    settings = get_settings()
    token = jwt.encode(payload, settings.jwt_secret_key, algorithm=settings.jwt_algorithm)
    if isinstance(token, bytes):
        return token.decode("utf-8")
    return token


def create_access_token(user_id: str, username: str) -> tuple[str, int]:
    settings = get_settings()
    now = _now_utc()
    expiry = now + timedelta(minutes=settings.access_token_minutes)
    payload = {
        "sub": user_id,
        "username": username,
        "type": "access",
        "iat": int(now.timestamp()),
        "exp": int(expiry.timestamp()),
    }
    return _encode(payload), settings.access_token_minutes * 60


def create_refresh_token(user_id: str, token_id: str | None = None) -> tuple[str, str, datetime, int]:
    settings = get_settings()
    now = _now_utc()
    expiry = now + timedelta(days=settings.refresh_token_days)
    refresh_id = token_id or str(uuid.uuid4())
    payload = {
        "sub": user_id,
        "type": "refresh",
        "rid": refresh_id,
        "iat": int(now.timestamp()),
        "exp": int(expiry.timestamp()),
    }
    token = _encode(payload)
    return token, refresh_id, expiry, settings.refresh_token_days * 24 * 60 * 60


def decode_token(token: str, expected_type: str | None = None) -> dict[str, Any]:
    settings = get_settings()
    try:
        payload = jwt.decode(
            token,
            settings.jwt_secret_key,
            algorithms=[settings.jwt_algorithm],
            options={"require": ["exp", "iat", "sub", "type"]},
        )
    except jwt.ExpiredSignatureError as exc:
        raise TokenError("Token expired") from exc
    except jwt.InvalidTokenError as exc:
        raise TokenError("Invalid token") from exc

    token_type = payload.get("type")
    if expected_type and token_type != expected_type:
        raise TokenError(f"Expected {expected_type} token")
    return payload


def build_token_pair(user_id: str, username: str) -> TokenPair:
    access_token, access_ttl = create_access_token(user_id=user_id, username=username)
    refresh_token, refresh_id, refresh_expiry, refresh_ttl = create_refresh_token(user_id=user_id)
    return TokenPair(
        access_token=access_token,
        access_expires_in=access_ttl,
        refresh_token=refresh_token,
        refresh_expires_in=refresh_ttl,
        refresh_token_id=refresh_id,
        refresh_expires_at=refresh_expiry,
    )


def hash_refresh_token(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()
