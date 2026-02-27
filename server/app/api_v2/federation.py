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
    FederationGroupCallEndRequestV2,
    FederationGroupCallJoinRequestV2,
    FederationGroupCallLeaveRequestV2,
    FederationGroupCallOfferRequestV2,
    FederationGroupCallWebRtcAnswerRequestV2,
    FederationGroupCallWebRtcIceRequestV2,
    FederationGroupCallWebRtcOfferRequestV2,
    FederationGroupEventRequestV2,
    FederationGroupInviteAcceptRequestV2,
    FederationMessageRelayRequestV2,
    FederationGroupSnapshotOutV2,
    FederationWellKnownOutV2,
)
from app.schemas.v2_device import DeviceOutV2, UserDeviceLookupV2
from app.services.device_service_v2 import device_service_v2
from app.services.call_service import CallProtocolError, call_service
from app.services.federation_security import federation_security_service
from app.services.group_call_service import group_call_service
from app.services.group_conversation_service import group_conversation_service
from app.services.message_service_v2 import message_service_v2
from app.services.metrics import metrics
from app.services.prekey_service_v2 import prekey_service_v2
from app.services.rate_limit import rate_limiter
from app.services.server_identity import (
    get_federation_signing_public_key_b64,
    get_server_onion,
    server_address_for_username,
)

router = APIRouter(prefix="/api/v2/federation", tags=["federation-v2"])


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
    settings = get_settings()
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-federation-write"), limit=240)
    raw_body = await request.body()
    if len(raw_body) > settings.max_federation_body_bytes:
        await metrics.inc("attachments.send.rejected_too_large")
        raise HTTPException(status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE, detail="Federation payload too large")
    await federation_security_service.verify_incoming(session, request, raw_body)
    sender = request.headers.get("x-bw-sender", "unknown").strip().lower() or "unknown"
    try:
        await rate_limiter.enforce_weighted(
            f"v2-federation-bytes:{sender}",
            units=len(raw_body),
            limit=settings.federation_bytes_per_minute_per_peer,
        )
    except HTTPException as exc:
        if exc.status_code == status.HTTP_429_TOO_MANY_REQUESTS:
            await metrics.inc("attachments.send.rejected_rate_limited")
        raise
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
        attachment_inline_max_bytes=settings.effective_attachment_inline_max_bytes(),
        max_ciphertext_bytes=settings.effective_max_ciphertext_bytes(),
        attachment_hard_ceiling_bytes=settings.attachment_hard_ceiling_bytes,
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
        attachment_inline_max_bytes=get_settings().effective_attachment_inline_max_bytes(),
        max_ciphertext_bytes=get_settings().effective_max_ciphertext_bytes(),
        attachment_policy_source="local",
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
    try:
        await message_service_v2.relay_message_from_federation(session, payload)
    except HTTPException as exc:
        if exc.status_code == status.HTTP_413_REQUEST_ENTITY_TOO_LARGE:
            await metrics.inc("attachments.send.rejected_too_large")
        raise
    return {"status": "ok"}


@router.post("/groups/events")
async def relay_group_event(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupEventRequestV2.model_validate_json(raw_body)
    sender_onion = request.headers.get("x-bw-sender", "").strip().lower() or None
    await group_conversation_service.apply_federation_event(
        session,
        payload=payload,
        sender_onion=sender_onion,
    )
    return {"status": "ok"}


@router.get("/groups/{group_uid}/snapshot", response_model=FederationGroupSnapshotOutV2)
async def group_snapshot(
    request: Request,
    group_uid: str,
    session: AsyncSession = Depends(db_session),
) -> FederationGroupSnapshotOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-federation-group-snapshot"), limit=120)
    await federation_security_service.verify_incoming(session, request, b"")
    return await group_conversation_service.snapshot_for_group(session, group_uid)


@router.post("/groups/invites/accept")
async def group_invite_accept(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupInviteAcceptRequestV2.model_validate_json(raw_body)
    await group_conversation_service.accept_remote_invite_to_origin(
        session,
        group_uid=payload.group_uid,
        actor_address=payload.actor_address,
    )
    return {"status": "ok"}


@router.post("/group-calls/offer")
async def relay_group_call_offer(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallOfferRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_offer(session, payload)
    return {"status": "ok"}


@router.post("/group-calls/join")
async def relay_group_call_join(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallJoinRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_join(session, payload)
    return {"status": "ok"}


@router.post("/group-calls/leave")
async def relay_group_call_leave(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallLeaveRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_leave(session, payload)
    return {"status": "ok"}


@router.post("/group-calls/end")
async def relay_group_call_end(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallEndRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_end(session, payload)
    return {"status": "ok"}


@router.post("/group-calls/webrtc-offer")
async def relay_group_call_webrtc_offer(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallWebRtcOfferRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_webrtc_offer(session, payload)
    return {"status": "ok"}


@router.post("/group-calls/webrtc-answer")
async def relay_group_call_webrtc_answer(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallWebRtcAnswerRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_webrtc_answer(session, payload)
    return {"status": "ok"}


@router.post("/group-calls/webrtc-ice")
async def relay_group_call_webrtc_ice(
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> dict[str, str]:
    raw_body = await _verify_federation_write_auth(request, session)
    payload = FederationGroupCallWebRtcIceRequestV2.model_validate_json(raw_body)
    await group_call_service.relay_webrtc_ice(session, payload)
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
