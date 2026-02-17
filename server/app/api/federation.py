from fastapi import APIRouter, Depends, HTTPException, Path, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session
from app.schemas.device import DeviceOut, UserDeviceLookup
from app.schemas.federation import (
    FederationCallAcceptRequest,
    FederationCallAudioRequest,
    FederationCallEndRequest,
    FederationCallOfferRequest,
    FederationCallRejectRequest,
    FederationMessageRelayRequest,
    FederationWellKnownOut,
)
from app.services.call_service import CallProtocolError, call_service
from app.services.device_service import device_service
from app.services.federation_security import federation_security_service
from app.services.message_service import message_service
from app.services.rate_limit import rate_limiter
from app.services.server_identity import (
    get_federation_signing_public_key_b64,
    get_server_onion,
    server_address_for_username,
)

router = APIRouter(prefix="/federation", tags=["federation"])
_MAX_FEDERATION_BODY_BYTES = 131072


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
    await rate_limiter.enforce(client_rate_limit_key(request, "federation-write"), limit=240)
    raw_body = await request.body()
    if len(raw_body) > _MAX_FEDERATION_BODY_BYTES:
        raise HTTPException(status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE, detail="Federation payload too large")
    await federation_security_service.verify_incoming(session, request, raw_body)
    return raw_body


@router.get("/well-known", response_model=FederationWellKnownOut)
async def well_known() -> FederationWellKnownOut:
    return FederationWellKnownOut(
        server_onion=get_server_onion(),
        federation_version="1",
        signing_public_key=get_federation_signing_public_key_b64(),
    )


@router.get("/users/{username}/device", response_model=UserDeviceLookup)
async def get_local_user_device(
    request: Request,
    username: str = Path(pattern=r"^[A-Za-z0-9_]{3,64}$"),
    session: AsyncSession = Depends(db_session),
) -> UserDeviceLookup:
    await rate_limiter.enforce(client_rate_limit_key(request, "federation-device-lookup"), limit=120)
    result = await device_service.get_active_device_by_username(session, username)
    if result is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Active device not found")
    user, device = result
    return UserDeviceLookup(
        username=user.username,
        peer_address=server_address_for_username(user.username),
        device=DeviceOut.model_validate(device),
    )


@router.post("/messages/relay")
async def relay_message(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationMessageRelayRequest.model_validate_json(raw_body)
    await message_service.relay_message_from_federation(session, payload)
    return {"status": "ok"}


@router.post("/calls/offer")
async def relay_call_offer(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallOfferRequest.model_validate_json(raw_body)
    try:
        await call_service.relay_offer(session, payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}


@router.post("/calls/accept")
async def relay_call_accept(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallAcceptRequest.model_validate_json(raw_body)
    try:
        await call_service.relay_accept(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}


@router.post("/calls/reject")
async def relay_call_reject(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallRejectRequest.model_validate_json(raw_body)
    try:
        await call_service.relay_reject(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}


@router.post("/calls/end")
async def relay_call_end(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallEndRequest.model_validate_json(raw_body)
    try:
        await call_service.relay_end(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}


@router.post("/calls/audio")
async def relay_call_audio(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationCallAudioRequest.model_validate_json(raw_body)
    try:
        await call_service.relay_audio(payload)
    except CallProtocolError as exc:
        _raise_for_call_error(exc)
    return {"status": "ok"}
