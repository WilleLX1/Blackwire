from fastapi import APIRouter, Depends, HTTPException, Path, Request, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session
from app.models.user import User
from app.schemas.v2_federation import (
    FederationMessageRelayRequestV2,
    FederationWellKnownOutV2,
)
from app.schemas.v2_device import DeviceOutV2, UserDeviceLookupV2
from app.services.device_service_v2 import device_service_v2
from app.services.federation_security import federation_security_service
from app.services.message_service_v2 import message_service_v2
from app.services.rate_limit import rate_limiter
from app.services.server_identity import (
    get_federation_signing_public_key_b64,
    get_server_onion,
    server_address_for_username,
)

router = APIRouter(prefix="/api/v2/federation", tags=["federation-v2"])
_MAX_FEDERATION_BODY_BYTES = 262144


async def _verify_federation_write_auth(
    request: Request,
    session: AsyncSession,
) -> bytes:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-federation-write"), limit=240)
    raw_body = await request.body()
    if len(raw_body) > _MAX_FEDERATION_BODY_BYTES:
        raise HTTPException(status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE, detail="Federation payload too large")
    await federation_security_service.verify_incoming(session, request, raw_body)
    return raw_body


@router.get("/well-known", response_model=FederationWellKnownOutV2)
async def well_known() -> FederationWellKnownOutV2:
    return FederationWellKnownOutV2(
        server_onion=get_server_onion(),
        federation_version="2",
        signing_public_key=get_federation_signing_public_key_b64(),
        identity_binding_mode="tor_v3_same_ed25519",
    )


@router.get("/users/{username}/devices", response_model=UserDeviceLookupV2)
async def get_local_user_devices(
    request: Request,
    username: str = Path(pattern=r"^[A-Za-z0-9_]{3,64}$"),
    session: AsyncSession = Depends(db_session),
) -> UserDeviceLookupV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-federation-device-lookup"), limit=120)
    normalized = username.strip().lower()
    user_stmt = select(User).where(User.username == normalized, User.disabled_at.is_(None))
    user = (await session.execute(user_stmt)).scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="User not found")

    devices = await device_service_v2.list_active_for_user(session, user.id)
    return UserDeviceLookupV2(
        username=user.username,
        peer_address=server_address_for_username(user.username),
        devices=[
            DeviceOutV2(
                device_uid=device.id,
                user_id=device.user_id,
                label=device.label,
                pub_sign_key=device.ik_ed25519_pub,
                pub_dh_key=device.enc_x25519_pub,
                status=device.status,
                created_at=device.created_at,
                last_seen_at=device.last_seen_at,
                revoked_at=device.revoked_at,
            )
            for device in devices
        ],
    )


@router.post("/messages/relay")
async def relay_message(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationMessageRelayRequestV2.model_validate_json(raw_body)
    await message_service_v2.relay_message_from_federation(session, payload)
    return {"status": "ok"}
