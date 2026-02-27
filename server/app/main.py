import asyncio
import json
import logging

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import ValidationError

from app.api import auth, conversations, devices, federation, health, messages, metrics, users
from app.api_v2 import (
    auth as auth_v2,
    conversations as conversations_v2,
    devices as devices_v2,
    federation as federation_v2,
    keys as keys_v2,
    messages as messages_v2,
    presence as presence_v2,
    users as users_v2,
    ws as ws_v2,
)
from app.config import get_settings
from app.db import get_session_factory, init_engine, init_models
from app.logging_utils import RequestContextMiddleware, configure_logging
from app.schemas.call import (
    CallAcceptRequest,
    CallAudioRequest,
    CallEndRequest,
    CallOfferRequest,
    CallRejectRequest,
)
from app.security.tokens import TokenError, decode_token
from app.services.call_service import CallProtocolError, call_service
from app.services.federation_outbox_service import federation_outbox_service
from app.services.message_service import message_service
from app.services.message_service_v2 import message_service_v2
from app.services.queue_worker import queue_cleanup_worker
from app.services.rate_limit import rate_limiter
from app.services.server_identity import (
    get_server_onion,
    get_server_onion_source,
    initialize_server_identity,
)
from app.ws.manager import connection_manager

logger = logging.getLogger("blackwire.app")
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


def create_app() -> FastAPI:
    settings = get_settings()
    configure_logging()

    app = FastAPI(title=settings.app_name, version="0.1.0")

    app.add_middleware(RequestContextMiddleware)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=settings.allow_origins,
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    app.include_router(health.router)
    app.include_router(auth.router, prefix=settings.api_prefix)
    app.include_router(users.router, prefix=settings.api_prefix)
    app.include_router(devices.router, prefix=settings.api_prefix)
    app.include_router(conversations.router, prefix=settings.api_prefix)
    app.include_router(messages.router, prefix=settings.api_prefix)
    app.include_router(federation.router, prefix=settings.api_prefix)
    app.include_router(metrics.router, prefix=settings.api_prefix)
    app.include_router(auth_v2.router)
    app.include_router(devices_v2.router)
    app.include_router(keys_v2.router)
    app.include_router(users_v2.router)
    app.include_router(presence_v2.router)
    app.include_router(conversations_v2.router)
    app.include_router(messages_v2.router)
    app.include_router(federation_v2.router)

    @app.on_event("startup")
    async def on_startup() -> None:
        if len(settings.jwt_secret_key.encode("utf-8")) < 32:
            raise RuntimeError("BLACKWIRE_JWT_SECRET_KEY must be at least 32 bytes")
        if settings.environment != "dev" and "*" in settings.allow_origins:
            raise RuntimeError("Wildcard CORS origin is not allowed outside dev")
        if settings.enable_webrtc_v2b2 and not settings.webrtc_ice_servers_json.strip():
            raise RuntimeError(
                "BLACKWIRE_WEBRTC_ICE_SERVERS_JSON is required when BLACKWIRE_ENABLE_WEBRTC_V2B2=true"
            )
        if settings.enable_webrtc_v2b2 and settings.webrtc_ice_servers_json.strip():
            try:
                parsed_ice = json.loads(settings.webrtc_ice_servers_json)
            except json.JSONDecodeError as exc:
                raise RuntimeError("BLACKWIRE_WEBRTC_ICE_SERVERS_JSON must be valid JSON") from exc
            if not isinstance(parsed_ice, list) or not parsed_ice:
                raise RuntimeError("BLACKWIRE_WEBRTC_ICE_SERVERS_JSON must be a non-empty JSON array")

        initialize_server_identity(settings)
        server_onion = get_server_onion()
        onion_source = get_server_onion_source()
        logger.info(
            "Server identity initialized: %s (source=%s, url=http://%s)",
            server_onion,
            onion_source,
            server_onion,
        )
        init_engine()
        if settings.auto_create_tables:
            await init_models()
        await rate_limiter.start()

        stop_event = asyncio.Event()
        app.state.queue_stop_event = stop_event

        async def cleanup_once() -> int:
            session_factory = get_session_factory()
            async with session_factory() as session:
                expired_v1 = await message_service.expire_old(session)
                expired_v2 = await message_service_v2.expire_old(session)
                return expired_v1 + expired_v2

        async def federation_outbox_once() -> int:
            session_factory = get_session_factory()
            async with session_factory() as session:
                return await federation_outbox_service.process_due(session)

        app.state.queue_cleanup_task = asyncio.create_task(
            queue_cleanup_worker(cleanup_once, stop_event)
        )
        app.state.federation_outbox_task = asyncio.create_task(
            queue_cleanup_worker(
                federation_outbox_once,
                stop_event,
                interval_seconds=max(1, settings.federation_outbox_poll_interval_seconds),
            )
        )

    @app.on_event("shutdown")
    async def on_shutdown() -> None:
        stop_event: asyncio.Event = app.state.queue_stop_event
        stop_event.set()

        task: asyncio.Task = app.state.queue_cleanup_task
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
        federation_task: asyncio.Task = app.state.federation_outbox_task
        federation_task.cancel()
        try:
            await federation_task
        except asyncio.CancelledError:
            pass

        await rate_limiter.close()

    @app.websocket(f"{settings.api_prefix}/ws")
    async def websocket_endpoint(websocket: WebSocket) -> None:
        token = _extract_bearer_token(websocket)
        if not token:
            await websocket.close(code=1008, reason="Missing bearer auth token")
            return

        try:
            payload = decode_token(token, expected_type="access")
        except TokenError:
            await websocket.close(code=1008, reason="Invalid access token")
            return

        user_id = payload.get("sub")
        if not user_id:
            await websocket.close(code=1008, reason="Invalid access token")
            return

        await connection_manager.connect(user_id=user_id, websocket=websocket)

        async def send_call_error(code: str, detail: str) -> None:
            await websocket.send_json({"type": "call.error", "code": code, "detail": detail})

        session_factory = get_session_factory()
        async with session_factory() as initial_session:
            await message_service.drain_pending_for_websocket(initial_session, user_id, websocket)

        try:
            while True:
                incoming = await websocket.receive_json()
                msg_type = incoming.get("type")

                if msg_type == "message.ack":
                    message_id = incoming.get("message_id")
                    if not message_id:
                        await websocket.send_json(
                            {"type": "error", "code": "invalid_ack", "detail": "message_id required"}
                        )
                        continue

                    async with session_factory() as ack_session:
                        await message_service.acknowledge(
                            ack_session,
                            user_id=user_id,
                            message_id=message_id,
                        )
                    continue

                if msg_type == "call.offer":
                    try:
                        payload = CallOfferRequest.model_validate(incoming)
                        async with session_factory() as call_session:
                            await call_service.offer(call_session, user_id, payload)
                    except ValidationError as exc:
                        await send_call_error("invalid_call_offer", str(exc))
                    except CallProtocolError as exc:
                        await send_call_error(exc.code, exc.detail)
                    continue

                if msg_type == "call.accept":
                    try:
                        payload = CallAcceptRequest.model_validate(incoming)
                        await call_service.accept(user_id, payload)
                    except ValidationError as exc:
                        await send_call_error("invalid_call_accept", str(exc))
                    except CallProtocolError as exc:
                        await send_call_error(exc.code, exc.detail)
                    continue

                if msg_type == "call.reject":
                    try:
                        payload = CallRejectRequest.model_validate(incoming)
                        await call_service.reject(user_id, payload)
                    except ValidationError as exc:
                        await send_call_error("invalid_call_reject", str(exc))
                    except CallProtocolError as exc:
                        await send_call_error(exc.code, exc.detail)
                    continue

                if msg_type == "call.end":
                    try:
                        payload = CallEndRequest.model_validate(incoming)
                        await call_service.end(user_id, payload)
                    except ValidationError as exc:
                        await send_call_error("invalid_call_end", str(exc))
                    except CallProtocolError as exc:
                        await send_call_error(exc.code, exc.detail)
                    continue

                if msg_type == "call.audio":
                    try:
                        payload = CallAudioRequest.model_validate(incoming)
                        await call_service.audio(user_id, payload)
                    except ValidationError as exc:
                        await send_call_error("invalid_call_audio", str(exc))
                    except CallProtocolError as exc:
                        await send_call_error(exc.code, exc.detail)
                    continue

                await websocket.send_json(
                    {
                        "type": "error",
                        "code": "unsupported_event",
                        "detail": (
                            "Supported client events: "
                            "message.ack, call.offer, call.accept, call.reject, call.end, call.audio"
                        ),
                    }
                )

        except WebSocketDisconnect:
            logger.info("websocket_disconnect", extra={"user_id": user_id})
        finally:
            await call_service.handle_disconnect(user_id)
            await connection_manager.disconnect(user_id=user_id, websocket=websocket)

    @app.websocket("/api/v2/ws")
    async def websocket_endpoint_v2(websocket: WebSocket) -> None:
        await ws_v2.websocket_endpoint_v2(websocket)

    return app


app = create_app()
