from datetime import UTC, datetime

from fastapi import HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.device import ActiveDevice, Device
from app.models.user import User
from app.schemas.device import DeviceOut, DeviceRegisterRequest, UserDeviceLookup
from app.services.federation_client import FederationClientError, federation_client
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_authority import is_local_server_authority


class DeviceService:
    def __init__(self) -> None:
        self.settings = get_settings()

    async def register_device(
        self,
        session: AsyncSession,
        user: User,
        payload: DeviceRegisterRequest,
    ) -> Device:
        existing_key_stmt = select(Device).where(Device.enc_x25519_pub == payload.enc_x25519_pub)
        existing_key = (await session.execute(existing_key_stmt)).scalar_one_or_none()
        if existing_key is not None:
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Device key already registered")

        device = Device(
            user_id=user.id,
            label=payload.label,
            ik_ed25519_pub=payload.ik_ed25519_pub,
            enc_x25519_pub=payload.enc_x25519_pub,
            status="active",
        )
        session.add(device)
        await session.flush()

        current_stmt = select(ActiveDevice).where(ActiveDevice.user_id == user.id)
        current_active = (await session.execute(current_stmt)).scalar_one_or_none()
        if current_active is not None:
            old_device_stmt = select(Device).where(Device.id == current_active.device_id)
            old_device = (await session.execute(old_device_stmt)).scalar_one_or_none()
            if old_device is not None:
                old_device.revoked_at = datetime.now(UTC)
                old_device.status = "revoked"
            current_active.device_id = device.id
            current_active.updated_at = datetime.now(UTC)
        else:
            session.add(ActiveDevice(user_id=user.id, device_id=device.id))

        await session.commit()
        await session.refresh(device)
        return device

    async def get_active_device_for_user(self, session: AsyncSession, user_id: str) -> Device | None:
        stmt = (
            select(Device)
            .join(ActiveDevice, ActiveDevice.device_id == Device.id)
            .where(
                ActiveDevice.user_id == user_id,
                Device.revoked_at.is_(None),
                Device.status == "active",
            )
        )
        return (await session.execute(stmt)).scalar_one_or_none()

    async def get_active_device_by_username(self, session: AsyncSession, username: str) -> tuple[User, Device] | None:
        normalized_username = username.strip().lower()
        user_stmt = select(User).where(User.username == normalized_username, User.disabled_at.is_(None))
        user = (await session.execute(user_stmt)).scalar_one_or_none()
        if user is None:
            return None

        device = await self.get_active_device_for_user(session, user.id)
        if device is None:
            return None
        return user, device

    async def resolve_device_by_peer_address(
        self,
        session: AsyncSession,
        peer_address: str,
        request_authority: str | None = None,
    ) -> UserDeviceLookup | None:
        try:
            parsed = parse_peer_address_with_policy(peer_address, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
        additional_aliases = {request_authority} if request_authority else None
        if is_local_server_authority(parsed.server_onion, self.settings, additional_aliases):
            local_result = await self.get_active_device_by_username(session, parsed.username)
            if local_result is None:
                return None
            user, device = local_result
            return UserDeviceLookup(
                username=user.username,
                peer_address=f"{user.username}@{parsed.server_onion}",
                device=DeviceOut.model_validate(device),
            )

        try:
            remote = await federation_client.get_remote_user_device(parsed.server_onion, parsed.username)
        except FederationClientError as exc:
            if exc.status_code == 404:
                return None
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=exc.detail,
            ) from exc

        if not remote.peer_address:
            remote.peer_address = parsed.canonical
        return remote


device_service = DeviceService()
