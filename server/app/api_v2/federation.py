from fastapi import APIRouter, Depends, HTTPException, Path, Request, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.config import get_settings
from app.dependencies import db_session
from app.models.user import User
from app.schemas.v2_federation import (
    FederationCallWebRtcAnswerRequestV2,
    FederationCallWebRtcIceRequestV2,
    FederationCallWebRtcOfferRequestV2,
    FederationMessageRelayRequestV2,
    FederationWellKnownOutV2,
)
from app.schemas.v2_device import DeviceOutV2, UserDeviceLookupV2
from app.services.device_service_v2 import device_service_v2
from app.services.call_service import CallProtocolError, call_service
from app.services.federation_security import federation_security_service
from app.services.message_service_v2 import message_service_v2
from app.services.prekey_service_v2 import prekey_service_v2
from app.services.rate_limit import rate_limiter
from app.services.server_identity import (
    get_federation_signing_public_key_b64,
    get_server_onion,
    server_address_for_username,
)

router = APIRouter(prefix="/api/v2/federation", tags=["federation-v2"])
_MAX_FEDERATION_BODY_BYTES = 262144


def _raise_for_call_error(exc: CallProtocolError) -> None:
    conflict_codes = {"peer_busy", "peer_offline", "invalid_state", "call_not_ringing"}
    not_found_codes = {"call_not_found", "conversation_not_found", "user_not_found"}
    forbidden_codes = {"forbidden", "invalid_target"}

    if exc.code in conflict_codes:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail=exc.detail) from exc
    if exc.code in not_found_codes:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=exc.detail) from exc
    if exc.code in forbidden_codes:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail=exc.detail) from exc
    raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=exc.detail) from exc


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
    settings = get_settings()
    supported_call_modes: list[str] = []
    if settings.enable_webrtc_v2b2:
        supported_call_modes.append("webrtc_v0_2b2")
    if settings.enable_legacy_call_audio_ws:
        supported_call_modes.append("ws_pcm_v0_2a")
    return FederationWellKnownOutV2(
        server_onion=get_server_onion(),
        federation_version="2",
        signing_public_key=get_federation_signing_public_key_b64(),
        identity_binding_mode="tor_v3_same_ed25519",
        supported_message_modes=device_service_v2.supported_message_modes(),
        supported_call_modes=supported_call_modes,
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
                supported_message_modes=device_service_v2.supported_message_modes(),
                created_at=device.created_at,
                last_seen_at=device.last_seen_at,
                revoked_at=device.revoked_at,
            )
            for device in devices
        ],
    )


@router.get("/users/{username}/prekeys")
async def get_local_user_prekeys(
    request: Request,
    username: str = Path(pattern=r"^[A-Za-z0-9_]{3,64}$"),
    session: AsyncSession = Depends(db_session),
) -> dict:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-federation-prekey-lookup"), limit=120)
    requested_by = request.headers.get("x-bw-sender", "federation@unknown").strip().lower() or "federation@unknown"
    result = await prekey_service_v2.resolve_local_user_prekeys_for_federation(session, username, requested_by)
    if result is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="User not found")
    return result.model_dump()


@router.post("/messages/relay")
async def relay_message(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationMessageRelayRequestV2.model_validate_json(raw_body)
    await message_service_v2.relay_message_from_federation(session, payload)
    return {"status": "ok"}


@router.post("/calls/webrtc-offer")
async def relay_webrtc_offer(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallWebRtcOfferRequestV2.model_validate_json(raw_body)
    try:
        await call_service.relay_webrtc_offer(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}


@router.post("/calls/webrtc-answer")
async def relay_webrtc_answer(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallWebRtcAnswerRequestV2.model_validate_json(raw_body)
    try:
        await call_service.relay_webrtc_answer(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}


@router.post("/calls/webrtc-ice")
async def relay_webrtc_ice(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallWebRtcIceRequestV2.model_validate_json(raw_body)
    try:
        await call_service.relay_webrtc_ice(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}
