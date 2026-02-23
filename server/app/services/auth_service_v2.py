import time
from datetime import UTC, datetime

from nacl import encoding, signing
from nacl import exceptions as nacl_exceptions
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.refresh_token import RefreshToken
from app.models.user import User
from app.schemas.v2_auth import (
    BindDeviceRequestV2,
    BootstrapTokenBundleV2,
    DeviceTokenBundleV2,
)
from app.schemas.v2_device import DeviceRegisterRequestV2
from app.security.tokens_v2 import (
    TokenErrorV2,
    build_device_token_pair,
    create_bootstrap_token,
    create_refresh_token,
    create_access_token,
    decode_token,
    hash_refresh_token,
)
from app.services.auth_service import AuthServiceError, auth_service
from app.services.device_service_v2 import device_service_v2


def canonical_bind_device_string(
    user_id: str,
    device_uid: str,
    nonce: str,
    timestamp_ms: int,
) -> bytes:
    return "\n".join(
        [
            "BIND_DEVICE",
            user_id.strip(),
            device_uid.strip(),
            nonce.strip(),
            str(timestamp_ms),
        ]
    ).encode("utf-8")


class AuthServiceV2:
    def __init__(self) -> None:
        self.settings = get_settings()

    async def register(self, session: AsyncSession, username: str, password: str) -> User:
        return await auth_service.register(session, username, password)

    async def authenticate(self, session: AsyncSession, username: str, password: str) -> User:
        return await auth_service.authenticate(session, username, password)

    async def issue_bootstrap(self, user: User) -> BootstrapTokenBundleV2:
        token, ttl = create_bootstrap_token(user.id, user.username)
        return BootstrapTokenBundleV2(
            bootstrap_token=token,
            token_type="bootstrap",
            bootstrap_expires_in=ttl,
        )

    async def issue_device_tokens(
        self,
        session: AsyncSession,
        user: User,
        device_uid: str,
    ) -> DeviceTokenBundleV2:
        token_pair = build_device_token_pair(user_id=user.id, username=user.username, device_uid=device_uid)
        refresh_record = RefreshToken(
            id=token_pair.refresh_token_id,
            user_id=user.id,
            device_id=device_uid,
            token_hash=hash_refresh_token(token_pair.refresh_token),
            expires_at=token_pair.refresh_expires_at,
            token_kind="refresh",
        )
        session.add(refresh_record)
        await session.commit()
        return DeviceTokenBundleV2(
            access_token=token_pair.access_token,
            access_expires_in=token_pair.access_expires_in,
            refresh_token=token_pair.refresh_token,
            refresh_expires_in=token_pair.refresh_expires_in,
            device_uid=device_uid,
        )

    async def register_device_from_bootstrap(
        self,
        session: AsyncSession,
        user: User,
        label: str,
        pub_sign_key: str,
        pub_dh_key: str,
    ) -> tuple[str, DeviceTokenBundleV2]:
        device = await device_service_v2.register_device(
            session,
            user,
            payload=DeviceRegisterRequestV2(
                label=label,
                pub_sign_key=pub_sign_key,
                pub_dh_key=pub_dh_key,
            ),
        )
        tokens = await self.issue_device_tokens(session, user, device.device_uid)
        return device.device_uid, tokens

    async def bind_device(
        self,
        session: AsyncSession,
        user: User,
        payload: BindDeviceRequestV2,
    ) -> DeviceTokenBundleV2:
        now_ms = int(time.time() * 1000)
        if abs(now_ms - payload.timestamp_ms) > (self.settings.v2_bind_request_skew_seconds * 1000):
            raise AuthServiceError("Bind request timestamp skew exceeded", status_code=401)

        device = await device_service_v2.get_device_for_user(session, user.id, payload.device_uid)
        if device is None:
            raise AuthServiceError("Device not found", status_code=404)
        if device.status != "active" or device.revoked_at is not None:
            raise AuthServiceError("Device revoked", status_code=401)

        canonical = canonical_bind_device_string(user.id, payload.device_uid, payload.nonce, payload.timestamp_ms)
        try:
            verify_key = signing.VerifyKey(device.ik_ed25519_pub.encode("utf-8"), encoder=encoding.Base64Encoder)
            signature = encoding.Base64Encoder.decode(payload.proof_signature_b64.encode("utf-8"))
            verify_key.verify(canonical, signature)
        except (ValueError, nacl_exceptions.BadSignatureError) as exc:
            raise AuthServiceError("Device proof signature invalid", status_code=401) from exc

        return await self.issue_device_tokens(session, user, device.id)

    async def refresh(self, session: AsyncSession, refresh_token_raw: str) -> tuple[User, DeviceTokenBundleV2]:
        try:
            payload = decode_token(refresh_token_raw, expected_type="refresh")
        except TokenErrorV2 as exc:
            raise AuthServiceError(str(exc), status_code=401) from exc

        user_id = str(payload.get("sub") or "")
        refresh_id = str(payload.get("rid") or "")
        device_uid = str(payload.get("did") or "")
        if not user_id or not refresh_id or not device_uid:
            raise AuthServiceError("Invalid refresh token payload", status_code=401)

        stmt = select(RefreshToken).where(
            RefreshToken.id == refresh_id,
            RefreshToken.user_id == user_id,
            RefreshToken.device_id == device_uid,
            RefreshToken.token_kind == "refresh",
        )
        current = (await session.execute(stmt)).scalar_one_or_none()
        if current is None:
            raise AuthServiceError("Refresh token not found", status_code=401)

        now = datetime.now(UTC)
        if current.revoked_at is not None:
            raise AuthServiceError("Refresh token revoked", status_code=401)
        if current.expires_at.astimezone(UTC) <= now:
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

        access_token, access_ttl = create_access_token(user.id, user.username, device_uid)
        new_refresh_token, new_refresh_id, new_expiry, new_refresh_ttl = create_refresh_token(user.id, device_uid)

        replacement = RefreshToken(
            id=new_refresh_id,
            user_id=user.id,
            device_id=device_uid,
            token_hash=hash_refresh_token(new_refresh_token),
            expires_at=new_expiry,
            token_kind="refresh",
        )
        session.add(replacement)
        await session.flush()

        current.revoked_at = now
        current.replaced_by = replacement.id
        await session.commit()

        bundle = DeviceTokenBundleV2(
            access_token=access_token,
            access_expires_in=access_ttl,
            refresh_token=new_refresh_token,
            refresh_expires_in=new_refresh_ttl,
            device_uid=device_uid,
        )
        return user, bundle

    async def logout(self, session: AsyncSession, refresh_token_raw: str) -> None:
        try:
            payload = decode_token(refresh_token_raw, expected_type="refresh")
        except TokenErrorV2:
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


auth_service_v2 = AuthServiceV2()
