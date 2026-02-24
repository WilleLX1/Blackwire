from datetime import UTC, datetime

from fastapi import HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.device import Device
from app.models.refresh_token import RefreshToken
from app.models.user import User
from app.schemas.v2_device import DeviceOutV2, DeviceRegisterRequestV2, UserDeviceLookupV2
from app.services.federation_client import FederationClientError, federation_client
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_authority import is_local_server_authority
from app.ws.manager import connection_manager


class DeviceServiceV2:
    def __init__(self) -> None:
        self.settings = get_settings()

    def supported_message_modes(self) -> list[str]:
        modes = ["sealedbox_v0_2a"]
        if self.settings.enable_ratchet_v2b1:
            modes.append("ratchet_v0_2b1")
        return modes

    def _to_device_out(self, device: Device) -> DeviceOutV2:
        return DeviceOutV2(
            device_uid=device.id,
            user_id=device.user_id,
            label=device.label,
            pub_sign_key=device.ik_ed25519_pub,
            pub_dh_key=device.enc_x25519_pub,
            status=device.status,
            supported_message_modes=self.supported_message_modes(),
            created_at=device.created_at,
            last_seen_at=device.last_seen_at,
            revoked_at=device.revoked_at,
        )

    def _local_attachment_inline_max_bytes(self) -> int:
        return self.settings.effective_attachment_inline_max_bytes()

    def _local_max_ciphertext_bytes(self) -> int:
        return self.settings.effective_max_ciphertext_bytes()

    async def register_device(
        self,
        session: AsyncSession,
        user: User,
        payload: DeviceRegisterRequestV2,
    ) -> DeviceOutV2:
        existing_key_stmt = select(Device).where(Device.enc_x25519_pub == payload.pub_dh_key)
        existing_key = (await session.execute(existing_key_stmt)).scalar_one_or_none()
        if existing_key is not None:
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Device key already registered")

        device = Device(
            user_id=user.id,
            label=payload.label,
            ik_ed25519_pub=payload.pub_sign_key,
            enc_x25519_pub=payload.pub_dh_key,
            status="active",
            last_seen_at=datetime.now(UTC),
            revoked_at=None,
        )
        session.add(device)
        await session.commit()
        await session.refresh(device)
        return self._to_device_out(device)

    async def list_for_user(self, session: AsyncSession, user_id: str) -> list[DeviceOutV2]:
        stmt = select(Device).where(Device.user_id == user_id).order_by(Device.created_at.asc())
        rows = list((await session.execute(stmt)).scalars().all())
        return [self._to_device_out(row) for row in rows]

    async def list_active_for_user(self, session: AsyncSession, user_id: str) -> list[Device]:
        stmt = (
            select(Device)
            .where(
                Device.user_id == user_id,
                Device.status == "active",
                Device.revoked_at.is_(None),
            )
            .order_by(Device.created_at.asc())
        )
        return list((await session.execute(stmt)).scalars().all())

    async def get_device_for_user(self, session: AsyncSession, user_id: str, device_uid: str) -> Device | None:
        stmt = select(Device).where(Device.id == device_uid, Device.user_id == user_id)
        return (await session.execute(stmt)).scalar_one_or_none()

    async def revoke_device(self, session: AsyncSession, user: User, device_uid: str) -> DeviceOutV2:
        device = await self.get_device_for_user(session, user.id, device_uid)
        if device is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Device not found")
        if device.status == "revoked":
            return self._to_device_out(device)

        now = datetime.now(UTC)
        device.status = "revoked"
        device.revoked_at = now
        device.last_seen_at = now

        refresh_tokens_stmt = select(RefreshToken).where(
            RefreshToken.user_id == user.id,
            RefreshToken.device_id == device.id,
            RefreshToken.revoked_at.is_(None),
            RefreshToken.token_kind == "refresh",
        )
        refresh_tokens = list((await session.execute(refresh_tokens_stmt)).scalars().all())
        for refresh in refresh_tokens:
            refresh.revoked_at = now

        await session.commit()
        await connection_manager.disconnect_device(device.id)
        return self._to_device_out(device)

    async def resolve_devices_by_peer_address(
        self,
        session: AsyncSession,
        peer_address: str,
        request_authority: str | None = None,
    ) -> UserDeviceLookupV2 | None:
        try:
            parsed = parse_peer_address_with_policy(peer_address, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc

        additional_aliases = {request_authority} if request_authority else None
        if is_local_server_authority(parsed.server_onion, self.settings, additional_aliases):
            user_stmt = select(User).where(User.username == parsed.username, User.disabled_at.is_(None))
            user = (await session.execute(user_stmt)).scalar_one_or_none()
            if user is None:
                return None
            devices = await self.list_active_for_user(session, user.id)
            return UserDeviceLookupV2(
                username=user.username,
                peer_address=parsed.canonical,
                devices=[self._to_device_out(device) for device in devices],
                attachment_inline_max_bytes=self._local_attachment_inline_max_bytes(),
                max_ciphertext_bytes=self._local_max_ciphertext_bytes(),
                attachment_policy_source="local",
            )

        try:
            remote = await federation_client.get_remote_user_devices_v2(parsed.server_onion, parsed.username)
        except FederationClientError as exc:
            if exc.status_code == 404:
                return None
            raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=exc.detail) from exc

        if not remote.peer_address:
            remote.peer_address = parsed.canonical
        remote.attachment_policy_source = "remote"
        if remote.attachment_inline_max_bytes <= 0 or remote.max_ciphertext_bytes <= 0:
            remote.attachment_inline_max_bytes = self._local_attachment_inline_max_bytes()
            remote.max_ciphertext_bytes = self._local_max_ciphertext_bytes()
            remote.attachment_policy_source = "fallback_local"
            await metrics.inc("attachments.policy.fallback_local")
        else:
            remote.attachment_inline_max_bytes = min(
                remote.attachment_inline_max_bytes,
                self.settings.attachment_hard_ceiling_bytes,
            )
            remote.max_ciphertext_bytes = min(
                remote.max_ciphertext_bytes,
                self.settings.attachment_hard_ceiling_bytes,
            )
        return remote


device_service_v2 = DeviceServiceV2()
