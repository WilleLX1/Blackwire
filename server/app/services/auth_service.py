import re
import string
from datetime import UTC, datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.refresh_token import RefreshToken
from app.models.user import User
from app.schemas.auth import TokenBundle
from app.security.password import hash_password, verify_password
from app.security.tokens import (
    TokenError,
    build_token_pair,
    create_access_token,
    create_refresh_token,
    decode_token,
    hash_refresh_token,
)
from app.services.metrics import metrics


class AuthServiceError(ValueError):
    def __init__(self, detail: str, status_code: int = 400) -> None:
        super().__init__(detail)
        self.status_code = status_code


_USERNAME_PATTERN = re.compile(r"^[a-z0-9_]{3,64}$")


def _as_utc(value: datetime) -> datetime:
    if value.tzinfo is None:
        return value.replace(tzinfo=UTC)
    return value.astimezone(UTC)


class AuthService:
    @staticmethod
    def _normalize_username(value: str) -> str:
        normalized = value.strip().lower()
        if not _USERNAME_PATTERN.fullmatch(normalized):
            raise AuthServiceError(
                "Username must be 3-64 chars and contain only lowercase letters, digits, or underscore",
                status_code=400,
            )
        return normalized

    @staticmethod
    def _validate_password_complexity(password: str) -> None:
        """Require at least one uppercase, one lowercase, one digit, and one special char."""
        if len(password) < 8:
            raise AuthServiceError("Password must be at least 8 characters", status_code=400)
        has_upper = any(c in string.ascii_uppercase for c in password)
        has_lower = any(c in string.ascii_lowercase for c in password)
        has_digit = any(c in string.digits for c in password)
        has_special = any(c in string.punctuation for c in password)
        if not (has_upper and has_lower and has_digit and has_special):
            raise AuthServiceError(
                "Password must contain uppercase, lowercase, digit, and special character",
                status_code=400,
            )

    async def register(self, session: AsyncSession, username: str, password: str) -> User:
        normalized_username = self._normalize_username(username)
        self._validate_password_complexity(password)
        exists_stmt = select(User).where(User.username == normalized_username)
        exists = (await session.execute(exists_stmt)).scalar_one_or_none()
        if exists is not None:
            raise AuthServiceError("Registration failed", status_code=409)

        user = User(username=normalized_username, password_hash=hash_password(password))
        session.add(user)
        await session.commit()
        await session.refresh(user)
        await metrics.inc("auth.register.success")
        return user

    async def authenticate(self, session: AsyncSession, username: str, password: str) -> User:
        normalized_username = self._normalize_username(username)
        stmt = select(User).where(User.username == normalized_username, User.disabled_at.is_(None))
        user = (await session.execute(stmt)).scalar_one_or_none()
        if user is None or not verify_password(password, user.password_hash):
            await metrics.inc("auth.login.failure")
            raise AuthServiceError("Invalid credentials", status_code=401)
        await metrics.inc("auth.login.success")
        return user

    async def issue_tokens(self, session: AsyncSession, user: User) -> TokenBundle:
        token_pair = build_token_pair(user_id=user.id, username=user.username)
        refresh_record = RefreshToken(
            id=token_pair.refresh_token_id,
            user_id=user.id,
            token_hash=hash_refresh_token(token_pair.refresh_token),
            expires_at=token_pair.refresh_expires_at,
        )
        session.add(refresh_record)
        await session.commit()

        return TokenBundle(
            access_token=token_pair.access_token,
            access_expires_in=token_pair.access_expires_in,
            refresh_token=token_pair.refresh_token,
            refresh_expires_in=token_pair.refresh_expires_in,
        )

    async def refresh(self, session: AsyncSession, refresh_token_raw: str) -> tuple[User, TokenBundle]:
        try:
            payload = decode_token(refresh_token_raw, expected_type="refresh")
        except TokenError as exc:
            raise AuthServiceError(str(exc), status_code=401) from exc

        user_id = payload.get("sub")
        refresh_id = payload.get("rid")
        if not user_id or not refresh_id:
            raise AuthServiceError("Invalid refresh token payload", status_code=401)

        stmt = select(RefreshToken).where(RefreshToken.id == refresh_id, RefreshToken.user_id == user_id)
        current = (await session.execute(stmt)).scalar_one_or_none()
        if current is None:
            raise AuthServiceError("Refresh token not found", status_code=401)

        now = datetime.now(UTC)
        if current.revoked_at is not None:
            raise AuthServiceError("Refresh token revoked", status_code=401)
        if _as_utc(current.expires_at) <= now:
            current.revoked_at = now
            await session.commit()
            raise AuthServiceError("Refresh token expired", status_code=401)
        if current.token_hash != hash_refresh_token(refresh_token_raw):
            current.revoked_at = now
            await session.commit()
            raise AuthServiceError("Refresh token mismatch", status_code=401)

        user_stmt = select(User).where(User.id == user_id, User.disabled_at.is_(None))
        user = (await session.execute(user_stmt)).scalar_one_or_none()
        if user is None:
            raise AuthServiceError("User not found", status_code=401)

        access_token, access_ttl = create_access_token(user_id=user.id, username=user.username)
        new_refresh_token, new_refresh_id, new_expiry, new_refresh_ttl = create_refresh_token(user.id)

        replacement = RefreshToken(
            id=new_refresh_id,
            user_id=user.id,
            token_hash=hash_refresh_token(new_refresh_token),
            expires_at=new_expiry,
        )
        session.add(replacement)
        # Ensure replacement row exists before setting FK pointer from current.replaced_by.
        await session.flush()

        current.revoked_at = now
        current.replaced_by = replacement.id
        await session.commit()

        bundle = TokenBundle(
            access_token=access_token,
            access_expires_in=access_ttl,
            refresh_token=new_refresh_token,
            refresh_expires_in=new_refresh_ttl,
        )
        return user, bundle

    async def logout(self, session: AsyncSession, refresh_token_raw: str) -> None:
        try:
            payload = decode_token(refresh_token_raw, expected_type="refresh")
        except TokenError:
            return

        refresh_id = payload.get("rid")
        user_id = payload.get("sub")
        if not refresh_id or not user_id:
            return

        stmt = select(RefreshToken).where(RefreshToken.id == refresh_id, RefreshToken.user_id == user_id)
        token = (await session.execute(stmt)).scalar_one_or_none()
        if token is None:
            return

        hashed = hash_refresh_token(refresh_token_raw)
        if token.token_hash != hashed:
            return

        token.revoked_at = datetime.now(UTC)
        await session.commit()


auth_service = AuthService()
