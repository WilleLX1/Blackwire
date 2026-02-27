from datetime import UTC, datetime, timedelta

from fastapi import HTTPException, status
from nacl import encoding, exceptions as nacl_exceptions, signing
from sqlalchemy import and_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.device import Device
from app.models.device_one_time_prekey import DeviceOneTimePrekey
from app.models.device_signed_prekey import DeviceSignedPrekey
from app.models.user import User
from app.schemas.v2_prekey import (
    OneTimePrekeyOutV2,
    PrekeyUploadRequestV2,
    PrekeyUploadResponseV2,
    ResolvePrekeysResponseV2,
    ResolvedPrekeyDeviceV2,
    SignedPrekeyOutV2,
)
from app.services.device_service_v2 import device_service_v2
from app.services.federation_client import FederationClientError, federation_client
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_authority import is_local_server_authority
from app.services.server_identity import get_server_onion, server_address_for_username


def canonical_signed_prekey_string(
    device_uid: str,
    key_id: int,
    pub_x25519_b64: str,
    expires_at: datetime,
) -> bytes:
    return "\n".join(
        [
            "SIGNED_PREKEY",
            device_uid.strip(),
            str(key_id),
            pub_x25519_b64.strip(),
            expires_at.astimezone(UTC).isoformat(),
        ]
    ).encode("utf-8")


class PrekeyServiceV2:
    def __init__(self) -> None:
        self.settings = get_settings()

    async def upload_prekeys(
        self,
        session: AsyncSession,
        user: User,
        device: Device,
        payload: PrekeyUploadRequestV2,
    ) -> PrekeyUploadResponseV2:
        expires_at = payload.signed_prekey.expires_at or (datetime.now(UTC) + timedelta(days=30))
        canonical = canonical_signed_prekey_string(
            device_uid=device.id,
            key_id=payload.signed_prekey.key_id,
            pub_x25519_b64=payload.signed_prekey.pub_x25519_b64,
            expires_at=expires_at,
        )
        try:
            verify_key = signing.VerifyKey(device.ik_ed25519_pub.encode("utf-8"), encoder=encoding.Base64Encoder)
            signature = encoding.Base64Encoder.decode(payload.signed_prekey.sig_by_device_sign_key_b64.encode("utf-8"))
            verify_key.verify(canonical, signature)
        except (ValueError, nacl_exceptions.BadSignatureError) as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid signed prekey signature") from exc

        signed_stmt = select(DeviceSignedPrekey).where(
            DeviceSignedPrekey.device_id == device.id,
            DeviceSignedPrekey.key_id == payload.signed_prekey.key_id,
        )
        signed = (await session.execute(signed_stmt)).scalar_one_or_none()
        if signed is None:
            signed = DeviceSignedPrekey(
                device_id=device.id,
                key_id=payload.signed_prekey.key_id,
                pub_x25519_b64=payload.signed_prekey.pub_x25519_b64,
                sig_by_device_sign_key_b64=payload.signed_prekey.sig_by_device_sign_key_b64,
                expires_at=expires_at,
                revoked_at=None,
            )
            session.add(signed)
        else:
            signed.pub_x25519_b64 = payload.signed_prekey.pub_x25519_b64
            signed.sig_by_device_sign_key_b64 = payload.signed_prekey.sig_by_device_sign_key_b64
            signed.expires_at = expires_at
            signed.revoked_at = None

        key_ids = {item.key_id for item in payload.one_time_prekeys}
        accepted = 0
        if key_ids:
            existing_stmt = select(DeviceOneTimePrekey.key_id).where(
                DeviceOneTimePrekey.device_id == device.id,
                DeviceOneTimePrekey.key_id.in_(key_ids),
            )
            existing = {row[0] for row in (await session.execute(existing_stmt)).all()}
            for item in payload.one_time_prekeys:
                if item.key_id in existing:
                    continue
                session.add(
                    DeviceOneTimePrekey(
                        device_id=device.id,
                        key_id=item.key_id,
                        pub_x25519_b64=item.pub_x25519_b64,
                    )
                )
                accepted += 1

        await session.commit()
        return PrekeyUploadResponseV2(
            uploaded_signed_prekey_key_id=payload.signed_prekey.key_id,
            accepted_one_time_prekeys=accepted,
        )

    async def resolve_prekeys(
        self,
        session: AsyncSession,
        requester_user: User,
        peer_address: str,
        request_authority: str | None = None,
    ) -> ResolvePrekeysResponseV2:
        parsed = parse_peer_address_with_policy(peer_address, self.settings.tor_enabled)
        additional_aliases = {request_authority} if request_authority else None
        if not is_local_server_authority(parsed.server_onion, self.settings, additional_aliases):
            try:
                return await federation_client.get_remote_user_prekeys_v2(parsed.server_onion, parsed.username)
            except FederationClientError as exc:
                if exc.status_code == 404:
                    raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Remote peer prekeys not found") from exc
                raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=exc.detail) from exc

        lookup = await device_service_v2.resolve_devices_by_peer_address(
            session,
            parsed.canonical,
            request_authority=request_authority,
        )
        if lookup is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Peer devices not found")

        requester_address = server_address_for_username(requester_user.username)
        out_devices: list[ResolvedPrekeyDeviceV2] = []
        opk_missing_count = 0
        now = datetime.now(UTC)
        for device_out in lookup.devices:
            signed_stmt = (
                select(DeviceSignedPrekey)
                .where(
                    DeviceSignedPrekey.device_id == device_out.device_uid,
                    DeviceSignedPrekey.revoked_at.is_(None),
                    DeviceSignedPrekey.expires_at > now,
                )
                .order_by(DeviceSignedPrekey.created_at.desc())
                .limit(1)
            )
            signed = (await session.execute(signed_stmt)).scalar_one_or_none()

            opk_stmt = (
                select(DeviceOneTimePrekey)
                .where(
                    DeviceOneTimePrekey.device_id == device_out.device_uid,
                    DeviceOneTimePrekey.consumed_at.is_(None),
                )
                .order_by(DeviceOneTimePrekey.created_at.asc())
                .limit(1)
                .with_for_update(skip_locked=True)
            )
            opk = (await session.execute(opk_stmt)).scalar_one_or_none()
            if opk is not None:
                opk.consumed_at = now
                opk.consumed_by_address = requester_address
            else:
                opk_missing_count += 1

            out_devices.append(
                ResolvedPrekeyDeviceV2(
                    device_uid=device_out.device_uid,
                    pub_sign_key=device_out.pub_sign_key,
                    pub_dh_key=device_out.pub_dh_key,
                    supported_message_modes=device_out.supported_message_modes,
                    signed_prekey=(
                        None
                        if signed is None
                        else SignedPrekeyOutV2(
                            key_id=signed.key_id,
                            pub_x25519_b64=signed.pub_x25519_b64,
                            sig_by_device_sign_key_b64=signed.sig_by_device_sign_key_b64,
                            expires_at=signed.expires_at,
                        )
                    ),
                    one_time_prekey=(
                        None
                        if opk is None
                        else OneTimePrekeyOutV2(
                            key_id=opk.key_id,
                            pub_x25519_b64=opk.pub_x25519_b64,
                        )
                    ),
                    opk_missing=opk is None,
                )
            )

        await session.commit()
        if opk_missing_count > 0:
            await metrics.inc("ratchet.prekey.opk.exhausted", opk_missing_count)
        return ResolvePrekeysResponseV2(
            username=lookup.username,
            peer_address=lookup.peer_address,
            devices=out_devices,
        )

    async def resolve_local_user_prekeys_for_federation(
        self,
        session: AsyncSession,
        username: str,
        requested_by_address: str,
    ) -> ResolvePrekeysResponseV2 | None:
        canonical = f"{username.strip().lower()}@{get_server_onion()}"
        parsed = parse_peer_address_with_policy(canonical, self.settings.tor_enabled)
        lookup = await device_service_v2.resolve_devices_by_peer_address(session, parsed.canonical)
        if lookup is None:
            return None

        out_devices: list[ResolvedPrekeyDeviceV2] = []
        opk_missing_count = 0
        now = datetime.now(UTC)
        for device_out in lookup.devices:
            signed_stmt = (
                select(DeviceSignedPrekey)
                .where(
                    DeviceSignedPrekey.device_id == device_out.device_uid,
                    DeviceSignedPrekey.revoked_at.is_(None),
                    DeviceSignedPrekey.expires_at > now,
                )
                .order_by(DeviceSignedPrekey.created_at.desc())
                .limit(1)
            )
            signed = (await session.execute(signed_stmt)).scalar_one_or_none()

            opk_stmt = (
                select(DeviceOneTimePrekey)
                .where(
                    DeviceOneTimePrekey.device_id == device_out.device_uid,
                    DeviceOneTimePrekey.consumed_at.is_(None),
                )
                .order_by(DeviceOneTimePrekey.created_at.asc())
                .limit(1)
                .with_for_update(skip_locked=True)
            )
            opk = (await session.execute(opk_stmt)).scalar_one_or_none()
            if opk is not None:
                opk.consumed_at = now
                opk.consumed_by_address = requested_by_address
            else:
                opk_missing_count += 1

            out_devices.append(
                ResolvedPrekeyDeviceV2(
                    device_uid=device_out.device_uid,
                    pub_sign_key=device_out.pub_sign_key,
                    pub_dh_key=device_out.pub_dh_key,
                    supported_message_modes=device_out.supported_message_modes,
                    signed_prekey=(
                        None
                        if signed is None
                        else SignedPrekeyOutV2(
                            key_id=signed.key_id,
                            pub_x25519_b64=signed.pub_x25519_b64,
                            sig_by_device_sign_key_b64=signed.sig_by_device_sign_key_b64,
                            expires_at=signed.expires_at,
                        )
                    ),
                    one_time_prekey=(
                        None
                        if opk is None
                        else OneTimePrekeyOutV2(
                            key_id=opk.key_id,
                            pub_x25519_b64=opk.pub_x25519_b64,
                        )
                    ),
                    opk_missing=opk is None,
                )
            )

        await session.commit()
        if opk_missing_count > 0:
            await metrics.inc("ratchet.prekey.opk.exhausted", opk_missing_count)
        return ResolvePrekeysResponseV2(
            username=lookup.username,
            peer_address=lookup.peer_address,
            devices=out_devices,
        )


prekey_service_v2 = PrekeyServiceV2()
