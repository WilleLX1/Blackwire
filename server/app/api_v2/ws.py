import logging

from fastapi import HTTPException, WebSocket, WebSocketDisconnect
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
from app.services.group_call_service import group_call_service
from app.services.message_service_v2 import message_service_v2
from app.services.conversation_service import conversation_service
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
        if group_call_service.settings.enable_group_call_v2c:
            await group_call_service.replay_pending_for_user(initial_session, user_id)

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
                        conversation = await conversation_service.get_by_id(call_session, payload_call.conversation_id)
                        if conversation is not None and conversation.conversation_type == "group":
                            await group_call_service.offer(call_session, user_id, payload_call.conversation_id)
                        else:
                            await call_service.offer(call_session, user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_offer", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_offer", str(exc.detail))
                continue

            if msg_type == "call.accept":
                try:
                    payload_call = CallAcceptRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            await group_call_service.join(call_session, user_id, payload_call.call_id)
                        else:
                            await call_service.accept(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_accept", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_accept", str(exc.detail))
                continue

            if msg_type == "call.reject":
                try:
                    payload_call = CallRejectRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            await group_call_service.reject(call_session, user_id, payload_call.call_id)
                        else:
                            await call_service.reject(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_reject", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_reject", str(exc.detail))
                continue

            if msg_type == "call.end":
                try:
                    payload_call = CallEndRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            await group_call_service.leave(
                                call_session,
                                user_id,
                                payload_call.call_id,
                                reason=(payload_call.reason or "left"),
                            )
                        else:
                            await call_service.end(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_end", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_end", str(exc.detail))
                continue

            if msg_type == "call.audio":
                if not call_service.settings.enable_legacy_call_audio_ws:
                    await send_call_error("audio_deprecated", "WS audio transport is disabled; use WebRTC")
                    continue
                try:
                    payload_call = CallAudioRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            await group_call_service.audio(call_session, user_id, payload_call)
                        else:
                            await call_service.audio(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_audio", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_audio", str(exc.detail))
                continue

            if msg_type == "call.webrtc.offer":
                try:
                    payload_call = CallWebRtcOfferRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            target = payload_call.target_user_address.strip().lower()
                            if not target:
                                raise HTTPException(status_code=400, detail="target_user_address is required for group signaling")
                            await group_call_service.route_webrtc_signal(
                                call_session,
                                sender_user_id=user_id,
                                call_id=payload_call.call_id,
                                event_type="call.webrtc.offer",
                                body={
                                    "sdp": payload_call.sdp,
                                    "call_schema_version": payload_call.call_schema_version,
                                    "call_mode": payload_call.call_mode,
                                    "max_participants": payload_call.max_participants,
                                },
                                target_user_address=target,
                            )
                        else:
                            await call_service.webrtc_offer(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_webrtc_offer", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_webrtc_offer", str(exc.detail))
                continue

            if msg_type == "call.webrtc.answer":
                try:
                    payload_call = CallWebRtcAnswerRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            target = payload_call.target_user_address.strip().lower()
                            if not target:
                                raise HTTPException(status_code=400, detail="target_user_address is required for group signaling")
                            await group_call_service.route_webrtc_signal(
                                call_session,
                                sender_user_id=user_id,
                                call_id=payload_call.call_id,
                                event_type="call.webrtc.answer",
                                body={
                                    "sdp": payload_call.sdp,
                                    "call_schema_version": payload_call.call_schema_version,
                                    "call_mode": payload_call.call_mode,
                                    "max_participants": payload_call.max_participants,
                                },
                                target_user_address=target,
                            )
                        else:
                            await call_service.webrtc_answer(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_webrtc_answer", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_webrtc_answer", str(exc.detail))
                continue

            if msg_type == "call.webrtc.ice":
                try:
                    payload_call = CallWebRtcIceRequest.model_validate(incoming)
                    async with session_factory() as call_session:
                        if await group_call_service.is_group_call(call_session, payload_call.call_id):
                            target = payload_call.target_user_address.strip().lower()
                            if not target:
                                raise HTTPException(status_code=400, detail="target_user_address is required for group signaling")
                            await group_call_service.route_webrtc_signal(
                                call_session,
                                sender_user_id=user_id,
                                call_id=payload_call.call_id,
                                event_type="call.webrtc.ice",
                                body={
                                    "candidate": payload_call.candidate,
                                    "sdp_mid": payload_call.sdp_mid,
                                    "sdp_mline_index": payload_call.sdp_mline_index,
                                    "call_schema_version": payload_call.call_schema_version,
                                    "call_mode": payload_call.call_mode,
                                    "max_participants": payload_call.max_participants,
                                },
                                target_user_address=target,
                            )
                        else:
                            await call_service.webrtc_ice(user_id, payload_call)
                except ValidationError as exc:
                    await send_call_error("invalid_call_webrtc_ice", str(exc))
                except CallProtocolError as exc:
                    await send_call_error(exc.code, exc.detail)
                except HTTPException as exc:
                    await send_call_error("invalid_call_webrtc_ice", str(exc.detail))
                continue

            await websocket.send_json(
                {
                    "type": "error",
                    "code": "unsupported_event",
                    "detail": (
                        "Supported client events: "
                        "message.ack, call.offer, call.accept, call.reject, call.end, "
                        "call.audio, call.webrtc.offer, call.webrtc.answer, call.webrtc.ice. "
                        "Typing and read updates are write-only over REST via "
                        "POST /api/v2/conversations/{conversation_id}/typing and "
                        "POST /api/v2/conversations/{conversation_id}/read."
                    ),
                }
            )
    except WebSocketDisconnect:
        logger.info("websocket_disconnect", extra={"user_id": user_id, "device_uid": device_uid})
    finally:
        async with session_factory() as disconnect_session:
            await group_call_service.handle_disconnect(disconnect_session, user_id)
        await call_service.handle_disconnect(user_id)
        await connection_manager.disconnect(user_id=user_id, websocket=websocket, device_uid=device_uid)
