import logging

from fastapi import WebSocket, WebSocketDisconnect
from pydantic import ValidationError
from sqlalchemy import select

from app.db import get_session_factory
from app.models.device import Device
from app.models.user import User
from app.schemas.call import (
    CallAcceptRequest,
    CallAudioRequest,
    CallEndRequest,
    CallOfferRequest,
    CallRejectRequest,
    CallWebRtcAnswerRequest,
    CallWebRtcIceRequest,
    CallWebRtcOfferRequest,
)
from app.security.tokens_v2 import TokenErrorV2, decode_token
from app.services.call_service import CallProtocolError, call_service
from app.services.message_service_v2 import message_service_v2
from app.ws.manager import connection_manager

logger = logging.getLogger("blackwire.app.v2.ws")
_BEARER_PREFIX = "bearer "


def _extract_bearer_token(websocket: WebSocket) -> str | None:
    auth_header = websocket.headers.get("authorization", "").strip()
    if auth_header.lower().startswith(_BEARER_PREFIX):
        token = auth_header[len(_BEARER_PREFIX) :].strip()
        if token:
            return token
    query_token = websocket.query_params.get("access_token", "").strip()
    if query_token:
        return query_token
    return None


async def websocket_endpoint_v2(websocket: WebSocket) -> None:
    token = _extract_bearer_token(websocket)
    if not token:
        await websocket.close(code=1008, reason="Missing bearer auth token")
        return

    try:
        payload = decode_token(token, expected_type="access")
    except TokenErrorV2:
        await websocket.close(code=1008, reason="Invalid access token")
        return

    user_id = str(payload.get("sub") or "")
    device_uid = str(payload.get("did") or "")
    if not user_id or not device_uid:
        await websocket.close(code=1008, reason="Invalid access token")
        return

    session_factory = get_session_factory()
    async with session_factory() as session:
        user = (await session.execute(select(User).where(User.id == user_id, User.disabled_at.is_(None)))).scalar_one_or_none()
        device = (
            await session.execute(
                select(Device).where(
                    Device.id == device_uid,
                    Device.user_id == user_id,
                    Device.status == "active",
                    Device.revoked_at.is_(None),
                )
            )
        ).scalar_one_or_none()
        if user is None or device is None:
            await websocket.close(code=1008, reason="Device revoked")
            return

    await connection_manager.connect(user_id=user_id, websocket=websocket, device_uid=device_uid)

    async def send_call_error(code: str, detail: str) -> None:
        await websocket.send_json({"type": "call.error", "code": code, "detail": detail})

    async with session_factory() as initial_session:
        await message_service_v2.drain_pending_for_websocket(initial_session, user_id, device_uid, websocket)

    try:
        while True:
            incoming = await websocket.receive_json()
            msg_type = incoming.get("type")

            if msg_type == "message.ack":
                copy_id = incoming.get("copy_id") or incoming.get("message_id")
                if not copy_id:
                    await websocket.send_json(
                        {"type": "error", "code": "invalid_ack", "detail": "copy_id required"}
                    )
                    continue
                async with session_factory() as ack_session:
                    await message_service_v2.acknowledge(
                        ack_session,
                        user_id=user_id,
                        device_uid=device_uid,
                        copy_id=copy_id,
                    )
                continue

            if msg_type == "call.offer":
                try:
                    payload_call = CallOfferRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        await call_service.offer(call_session, user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_offer", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.accept":
                try:
                    payload_call = CallAcceptRequest.model_validate(incoming)
                    await call_service.accept(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_accept", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.reject":
                try:
                    payload_call = CallRejectRequest.model_validate(incoming)
                    await call_service.reject(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_reject", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.end":
                try:
                    payload_call = CallEndRequest.model_validate(incoming)
                    await call_service.end(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_end", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.audio":
                if not call_service.settings.enable_legacy_call_audio_ws:
                    await send_call_error("audio_deprecated", "WS audio transport is disabled; use WebRTC")
                    continue
                try:
                    payload_call = CallAudioRequest.model_validate(incoming)
                    await call_service.audio(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_audio", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.webrtc.offer":
                try:
                    payload_call = CallWebRtcOfferRequest.model_validate(incoming)
                    await call_service.webrtc_offer(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_webrtc_offer", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.webrtc.answer":
                try:
                    payload_call = CallWebRtcAnswerRequest.model_validate(incoming)
                    await call_service.webrtc_answer(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_webrtc_answer", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            if msg_type == "call.webrtc.ice":
                try:
                    payload_call = CallWebRtcIceRequest.model_validate(incoming)
                    await call_service.webrtc_ice(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_webrtc_ice", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                continue

            await websocket.send_json(
                {
                    "type": "error",
                    "code": "unsupported_event",
                    "detail": (
                        "Supported client events: "
                        "message.ack, call.offer, call.accept, call.reject, call.end, "
                        "call.audio, call.webrtc.offer, call.webrtc.answer, call.webrtc.ice"
                    ),
                }
            )
    except WebSocketDisconnect:
        logger.info("websocket_disconnect", extra={"user_id": user_id, "device_uid": device_uid})
    finally:
        await call_service.handle_disconnect(user_id)
        await connection_manager.disconnect(user_id=user_id, websocket=websocket, device_uid=device_uid)
