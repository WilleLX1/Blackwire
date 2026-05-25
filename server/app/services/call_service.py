import asyncio
import base64
import binascii
import json
import time
from dataclasses import dataclass, field
from typing import Literal
from uuid import uuid4

from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
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
from app.schemas.federation import (
    FederationCallAcceptRequest,
    FederationCallAudioRequest,
    FederationCallEndRequest,
    FederationCallOfferRequest,
    FederationCallRejectRequest,
)
from app.schemas.v2_federation import (
    FederationCallWebRtcAnswerRequestV2,
    FederationCallWebRtcIceRequestV2,
    FederationCallWebRtcOfferRequestV2,
)
from app.services.conversation_service import conversation_service
from app.services.federation_client import FederationClientError, federation_client
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_identity import get_server_onion, server_address_for_username
from app.ws.manager import connection_manager


class CallProtocolError(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(detail)
        self.code = code
        self.detail = detail


@dataclass
class CallSession:
    call_id: str
    conversation_id: str
    caller_address: str
    callee_address: str
    direction: Literal["local", "federated_outbound", "federated_inbound"]
    caller_user_id: str | None = None
    callee_user_id: str | None = None
    state: Literal["ringing", "active"] = "ringing"
    timeout_task: asyncio.Task | None = None
    last_audio_at: dict[str, float] = field(default_factory=dict)

    @property
    def federated(self) -> bool:
        return self.direction != "local"

    def has_participant(self, user_id: str) -> bool:
        return user_id in (self.caller_user_id, self.callee_user_id)

    def peer_of(self, user_id: str) -> str | None:
        if self.direction != "local":
            return None
        if self.caller_user_id == user_id:
            return self.callee_user_id
        if self.callee_user_id == user_id:
            return self.caller_user_id
        return None

    def local_participants(self) -> list[str]:
        participants: list[str] = []
        if self.caller_user_id is not None:
            participants.append(self.caller_user_id)
        if self.callee_user_id is not None and self.callee_user_id != self.caller_user_id:
            participants.append(self.callee_user_id)
        return participants


class CallService:
    def __init__(self) -> None:
        self.settings = get_settings()
        self._calls: dict[str, CallSession] = {}
        self._user_to_call: dict[str, str] = {}
        self._lock = asyncio.Lock()

    def _webrtc_metadata(self) -> dict:
        if not self.settings.enable_webrtc_v2b2:
            return {}
        metadata: dict = {
            "call_schema_version": 1,
            "call_mode": "webrtc",
            "max_participants": 2,
        }
        try:
            parsed = json.loads(self.settings.webrtc_ice_servers_json) if self.settings.webrtc_ice_servers_json else []
        except json.JSONDecodeError:
            parsed = []
        if isinstance(parsed, list):
            metadata["ice_servers"] = parsed
        else:
            metadata["ice_servers"] = []
        return metadata

    async def _local_address_for_user_id(self, session: AsyncSession, user_id: str) -> str:
        stmt = select(User).where(User.id == user_id)
        user = (await session.execute(stmt)).scalar_one_or_none()
        if user is None:
            raise CallProtocolError("user_not_found", "Local user not found")
        return server_address_for_username(user.username)

    async def _local_user_for_address(self, session: AsyncSession, user_address: str) -> User:
        parsed = parse_peer_address_with_policy(user_address, self.settings.tor_enabled)
        if parsed.server_onion != get_server_onion():
            raise CallProtocolError("invalid_target", "Target user address is not local")
        stmt = select(User).where(User.username == parsed.username, User.disabled_at.is_(None))
        user = (await session.execute(stmt)).scalar_one_or_none()
        if user is None:
            raise CallProtocolError("user_not_found", "Local target user not found")
        return user

    async def offer(
        self,
        session: AsyncSession,
        caller_user_id: str,
        payload: CallOfferRequest,
    ) -> None:
        try:
            conversation = await conversation_service.get_by_id(session, payload.conversation_id)
            if conversation is None:
                raise CallProtocolError("conversation_not_found", "Conversation not found")
            conversation_service.ensure_membership(conversation, caller_user_id)
        except HTTPException as exc:
            raise CallProtocolError("invalid_offer", str(exc.detail)) from exc

        if conversation.kind == "remote":
            await self._offer_remote(session, caller_user_id, conversation.id)
            return

        try:
            callee_user_id = conversation_service.peer_id(conversation, caller_user_id)
            caller_address = await self._local_address_for_user_id(session, caller_user_id)
            callee_address = await self._local_address_for_user_id(session, callee_user_id)
        except HTTPException as exc:
            raise CallProtocolError("invalid_offer", str(exc.detail)) from exc

        incoming_payload = None
        ringing_payload = None
        busy_payload = None

        async with self._lock:
            if caller_user_id in self._user_to_call:
                busy_payload = {
                    "type": "call.busy",
                    "reason": "caller_busy",
                    "conversation_id": payload.conversation_id,
                }
            elif callee_user_id in self._user_to_call:
                busy_payload = {
                    "type": "call.busy",
                    "reason": "peer_busy",
                    "conversation_id": payload.conversation_id,
                }
            else:
                call_id = str(uuid4())
                call = CallSession(
                    call_id=call_id,
                    conversation_id=payload.conversation_id,
                    caller_user_id=caller_user_id,
                    callee_user_id=callee_user_id,
                    caller_address=caller_address,
                    callee_address=callee_address,
                    direction="local",
                )
                self._calls[call_id] = call
                self._user_to_call[caller_user_id] = call_id
                self._user_to_call[callee_user_id] = call_id
                call.timeout_task = asyncio.create_task(self._expire_ringing(call_id))

                incoming_payload = {
                    "type": "call.incoming",
                    "call_id": call_id,
                    "conversation_id": payload.conversation_id,
                    "from_user_id": caller_user_id,
                    "from_user_address": caller_address,
                }
                ringing_payload = {
                    "type": "call.ringing",
                    "call_id": call_id,
                    "conversation_id": payload.conversation_id,
                    "peer_user_id": callee_user_id,
                    "peer_user_address": callee_address,
                }

        if busy_payload is not None:
            await connection_manager.send_to_user(caller_user_id, busy_payload)
            return
        if incoming_payload is not None:
            await connection_manager.send_to_user(callee_user_id, incoming_payload)
        if ringing_payload is not None:
            await connection_manager.send_to_user(caller_user_id, ringing_payload)

    async def _offer_remote(self, session: AsyncSession, caller_user_id: str, conversation_id: str) -> None:
        conversation = await conversation_service.get_by_id(session, conversation_id)
        if conversation is None or conversation.kind != "remote":
            raise CallProtocolError("conversation_not_found", "Conversation not found")

        caller_address = await self._local_address_for_user_id(session, caller_user_id)
        callee_address = conversation.peer_address
        if not callee_address:
            raise CallProtocolError("invalid_offer", "Remote conversation has no peer address")

        call_id = str(uuid4())
        call = CallSession(
            call_id=call_id,
            conversation_id=conversation_id,
            caller_user_id=caller_user_id,
            callee_user_id=None,
            caller_address=caller_address,
            callee_address=callee_address,
            direction="federated_outbound",
        )

        async with self._lock:
            if caller_user_id in self._user_to_call:
                await connection_manager.send_to_user(
                    caller_user_id,
                    {
                        "type": "call.busy",
                        "reason": "caller_busy",
                        "conversation_id": conversation_id,
                    },
                )
                return
            self._calls[call_id] = call
            self._user_to_call[caller_user_id] = call_id
            call.timeout_task = asyncio.create_task(self._expire_ringing(call_id))

        relay_payload = {
            "relay_id": str(uuid4()),
            "call_id": call_id,
            "from_user_address": caller_address,
            "to_user_address": callee_address,
        }
        try:
            await federation_client.post_signed(
                conversation.peer_server_onion,
                f"{self.settings.api_prefix}/federation/calls/offer",
                relay_payload,
            )
        except FederationClientError as exc:
            async with self._lock:
                self._remove_call_locked(call_id)
            reason = "peer_unreachable" if exc.status_code >= 500 else "peer_rejected"
            await connection_manager.send_to_user(
                caller_user_id,
                {
                    "type": "call.rejected",
                    "call_id": call_id,
                    "conversation_id": conversation_id,
                    "reason": reason,
                },
            )
            return

        await connection_manager.send_to_user(
            caller_user_id,
            {
                "type": "call.ringing",
                "call_id": call_id,
                "conversation_id": conversation_id,
                "peer_user_id": "",
                "peer_user_address": callee_address,
            },
        )

    async def accept(self, user_id: str, payload: CallAcceptRequest) -> None:
        notify: list[tuple[str, dict]] = []
        relay_data: tuple[str, str, str] | None = None

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.state != "ringing":
                raise CallProtocolError("call_not_ringing", "Call is not awaiting acceptance")

            if call.direction == "local":
                if call.callee_user_id != user_id:
                    raise CallProtocolError("forbidden", "Only the callee can accept this call")

                call.state = "active"
                if call.timeout_task is not None:
                    call.timeout_task.cancel()
                    call.timeout_task = None

                notify = [
                    (
                        call.caller_user_id or "",
                        {
                            "type": "call.accepted",
                            "call_id": call.call_id,
                            "conversation_id": call.conversation_id,
                            "peer_user_id": call.callee_user_id or "",
                            "peer_user_address": call.callee_address,
                            **self._webrtc_metadata(),
                        },
                    ),
                    (
                        call.callee_user_id or "",
                        {
                            "type": "call.accepted",
                            "call_id": call.call_id,
                            "conversation_id": call.conversation_id,
                            "peer_user_id": call.caller_user_id or "",
                            "peer_user_address": call.caller_address,
                            **self._webrtc_metadata(),
                        },
                    ),
                ]
            elif call.direction == "federated_inbound":
                if call.callee_user_id != user_id:
                    raise CallProtocolError("forbidden", "Only the callee can accept this call")

                call.state = "active"
                if call.timeout_task is not None:
                    call.timeout_task.cancel()
                    call.timeout_task = None

                parsed_peer = parse_peer_address_with_policy(call.caller_address, self.settings.tor_enabled)
                relay_data = (parsed_peer.server_onion, call.callee_address, call.caller_address)
                notify = [
                    (
                        call.callee_user_id or "",
                        {
                            "type": "call.accepted",
                            "call_id": call.call_id,
                            "conversation_id": call.conversation_id,
                            "peer_user_id": "",
                            "peer_user_address": call.caller_address,
                            **self._webrtc_metadata(),
                        },
                    )
                ]
            else:
                raise CallProtocolError("forbidden", "Only the remote callee can accept this call")
        if relay_data is not None:
            peer_onion, from_user_address, to_user_address = relay_data
            try:
                await federation_client.post_signed(
                    peer_onion,
                    f"{self.settings.api_prefix}/federation/calls/accept",
                    {
                        "relay_id": str(uuid4()),
                        "call_id": payload.call_id,
                        "from_user_address": from_user_address,
                        "to_user_address": to_user_address,
                    },
                )
            except FederationClientError as exc:
                await connection_manager.send_to_user(
                    user_id,
                    {
                        "type": "call.error",
                        "code": "federation_accept_failed",
                        "detail": exc.detail,
                    },
                )
                await self._end_call(payload.call_id, "federation_error", by_user_id=user_id)
                return

        for target_user_id, payload_json in notify:
            if target_user_id:
                await connection_manager.send_to_user(target_user_id, payload_json)

    async def reject(self, user_id: str, payload: CallRejectRequest) -> None:
        reason = (payload.reason or "declined").strip().lower() or "declined"
        relay_data: tuple[str, str, str] | None = None
        notify: list[tuple[str, dict]] = []

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.state != "ringing":
                raise CallProtocolError("call_not_ringing", "Call is not awaiting acceptance")

            if call.direction == "local":
                if call.callee_user_id != user_id:
                    raise CallProtocolError("forbidden", "Only the callee can reject this call")
                caller_user_id = call.caller_user_id or ""
                callee_user_id = call.callee_user_id or ""
                call_id = call.call_id
                self._remove_call_locked(call_id)
                notify = [
                    (
                        caller_user_id,
                        {"type": "call.rejected", "call_id": call_id, "reason": reason},
                    ),
                    (
                        callee_user_id,
                        {
                            "type": "call.ended",
                            "call_id": call_id,
                            "reason": reason,
                            "by_user_id": callee_user_id,
                        },
                    ),
                ]
            elif call.direction == "federated_inbound":
                if call.callee_user_id != user_id:
                    raise CallProtocolError("forbidden", "Only the callee can reject this call")
                parsed_peer = parse_peer_address_with_policy(call.caller_address, self.settings.tor_enabled)
                relay_data = (parsed_peer.server_onion, call.callee_address, call.caller_address)
                local_user_id = call.callee_user_id or ""
                call_id = call.call_id
                self._remove_call_locked(call_id)
                notify = [
                    (
                        local_user_id,
                        {
                            "type": "call.ended",
                            "call_id": call_id,
                            "reason": reason,
                            "by_user_id": local_user_id,
                        },
                    )
                ]
            else:
                raise CallProtocolError("forbidden", "Only the local callee can reject this call")

        if relay_data is not None:
            peer_onion, from_user_address, to_user_address = relay_data
            try:
                await federation_client.post_signed(
                    peer_onion,
                    f"{self.settings.api_prefix}/federation/calls/reject",
                    {
                        "relay_id": str(uuid4()),
                        "call_id": payload.call_id,
                        "from_user_address": from_user_address,
                        "to_user_address": to_user_address,
                        "reason": reason,
                    },
                )
            except FederationClientError:
                pass

        for target_user_id, payload_json in notify:
            if target_user_id:
                await connection_manager.send_to_user(target_user_id, payload_json)

    async def end(self, user_id: str, payload: CallEndRequest) -> None:
        reason = (payload.reason or "ended").strip().lower() or "ended"

        relay_data: tuple[str, str, str] | None = None
        local_targets: list[str] = []

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if not call.has_participant(user_id):
                raise CallProtocolError("forbidden", "Not a participant in this call")

            local_targets = call.local_participants()
            if call.federated:
                if call.direction == "federated_outbound":
                    parsed_peer = parse_peer_address_with_policy(call.callee_address, self.settings.tor_enabled)
                    relay_data = (parsed_peer.server_onion, call.caller_address, call.callee_address)
                else:
                    parsed_peer = parse_peer_address_with_policy(call.caller_address, self.settings.tor_enabled)
                    relay_data = (parsed_peer.server_onion, call.callee_address, call.caller_address)

            self._remove_call_locked(payload.call_id)

        ended_payload = {
            "type": "call.ended",
            "call_id": payload.call_id,
            "reason": reason,
            "by_user_id": user_id,
        }
        for target in local_targets:
            await connection_manager.send_to_user(target, ended_payload)

        if relay_data is not None:
            peer_onion, from_user_address, to_user_address = relay_data
            try:
                await federation_client.post_signed(
                    peer_onion,
                    f"{self.settings.api_prefix}/federation/calls/end",
                    {
                        "relay_id": str(uuid4()),
                        "call_id": payload.call_id,
                        "from_user_address": from_user_address,
                        "to_user_address": to_user_address,
                        "reason": reason,
                    },
                )
            except FederationClientError:
                pass

    async def audio(self, user_id: str, payload: CallAudioRequest) -> None:
        if not self.settings.enable_legacy_call_audio_ws:
            raise CallProtocolError("audio_deprecated", "WS audio transport is disabled; use WebRTC")
        try:
            pcm = base64.b64decode(payload.pcm_b64.encode("utf-8"), validate=True)
        except binascii.Error as exc:
            raise CallProtocolError("invalid_audio", "Invalid audio payload encoding") from exc

        if len(pcm) == 0:
            return

        if len(pcm) > self.settings.voice_audio_max_chunk_bytes:
            raise CallProtocolError("audio_too_large", "Audio chunk too large")

        local_peer_user_id: str | None = None
        relay_data: tuple[str, str, str] | None = None
        out_payload: dict | None = None

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.state != "active":
                raise CallProtocolError("call_not_active", "Call is not active")
            if not call.has_participant(user_id):
                raise CallProtocolError("forbidden", "Not a participant in this call")

            now = time.monotonic()
            previous = call.last_audio_at.get(user_id)
            min_interval = max(0.001, float(self.settings.voice_audio_min_interval_ms) / 1000.0)
            if previous is not None and (now - previous) < min_interval:
                raise CallProtocolError("audio_rate_limited", "Audio frame rate too high")
            call.last_audio_at[user_id] = now

            if call.direction == "local":
                local_peer_user_id = call.peer_of(user_id)
                out_payload = {
                    "type": "call.audio",
                    "call_id": call.call_id,
                    "from_user_id": user_id,
                    "from_user_address": call.caller_address if user_id == call.caller_user_id else call.callee_address,
                    "sequence": payload.sequence,
                    "pcm_b64": payload.pcm_b64,
                }
            else:
                if call.direction == "federated_outbound":
                    parsed_peer = parse_peer_address_with_policy(call.callee_address, self.settings.tor_enabled)
                    relay_data = (parsed_peer.server_onion, call.caller_address, call.callee_address)
                else:
                    parsed_peer = parse_peer_address_with_policy(call.caller_address, self.settings.tor_enabled)
                    relay_data = (parsed_peer.server_onion, call.callee_address, call.caller_address)

        if local_peer_user_id is not None and out_payload is not None:
            await connection_manager.send_to_user(local_peer_user_id, out_payload)
            return

        if relay_data is not None:
            peer_onion, from_user_address, to_user_address = relay_data
            try:
                await federation_client.post_signed(
                    peer_onion,
                    f"{self.settings.api_prefix}/federation/calls/audio",
                    {
                        "relay_id": str(uuid4()),
                        "call_id": payload.call_id,
                        "from_user_address": from_user_address,
                        "to_user_address": to_user_address,
                        "sequence": payload.sequence,
                        "pcm_b64": payload.pcm_b64,
                    },
                )
            except FederationClientError as exc:
                raise CallProtocolError("federation_audio_failed", exc.detail) from exc

    def _validate_webrtc_enabled(self) -> None:
        if not self.settings.enable_webrtc_v2b2:
            raise CallProtocolError("webrtc_disabled", "WebRTC signaling is disabled")

    @staticmethod
    def _validate_call_mode(call_mode: str, max_participants: int) -> None:
        normalized_mode = (call_mode or "").strip().lower()
        if normalized_mode != "webrtc":
            raise CallProtocolError("invalid_call_mode", "call_mode must be 'webrtc'")
        if max_participants != 2:
            raise CallProtocolError("invalid_participant_limit", "Only 1:1 calls are supported in this release")

    async def webrtc_offer(self, user_id: str, payload: CallWebRtcOfferRequest) -> None:
        self._validate_webrtc_enabled()
        self._validate_call_mode(payload.call_mode, payload.max_participants)
        await self._relay_webrtc_signaling(
            user_id=user_id,
            call_id=payload.call_id,
            event_type="call.webrtc.offer",
            local_payload={
                "type": "call.webrtc.offer",
                "call_id": payload.call_id,
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
            federation_path="/api/v2/federation/calls/webrtc-offer",
            federation_payload={
                "call_id": payload.call_id,
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
        )

    async def webrtc_answer(self, user_id: str, payload: CallWebRtcAnswerRequest) -> None:
        self._validate_webrtc_enabled()
        self._validate_call_mode(payload.call_mode, payload.max_participants)
        await self._relay_webrtc_signaling(
            user_id=user_id,
            call_id=payload.call_id,
            event_type="call.webrtc.answer",
            local_payload={
                "type": "call.webrtc.answer",
                "call_id": payload.call_id,
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
            federation_path="/api/v2/federation/calls/webrtc-answer",
            federation_payload={
                "call_id": payload.call_id,
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
        )

    async def webrtc_ice(self, user_id: str, payload: CallWebRtcIceRequest) -> None:
        self._validate_webrtc_enabled()
        self._validate_call_mode(payload.call_mode, payload.max_participants)
        await self._relay_webrtc_signaling(
            user_id=user_id,
            call_id=payload.call_id,
            event_type="call.webrtc.ice",
            local_payload={
                "type": "call.webrtc.ice",
                "call_id": payload.call_id,
                "candidate": payload.candidate,
                "sdp_mid": payload.sdp_mid,
                "sdp_mline_index": payload.sdp_mline_index,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
            federation_path="/api/v2/federation/calls/webrtc-ice",
            federation_payload={
                "call_id": payload.call_id,
                "candidate": payload.candidate,
                "sdp_mid": payload.sdp_mid,
                "sdp_mline_index": payload.sdp_mline_index,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
        )

    async def _relay_webrtc_signaling(
        self,
        *,
        user_id: str,
        call_id: str,
        event_type: str,
        local_payload: dict,
        federation_path: str,
        federation_payload: dict,
    ) -> None:
        local_target: str | None = None
        relay_data: tuple[str, str, str] | None = None

        async with self._lock:
            call = self._calls.get(call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.state != "active":
                raise CallProtocolError("call_not_active", "Call is not active")
            if not call.has_participant(user_id):
                raise CallProtocolError("forbidden", "Not a participant in this call")

            if call.direction == "local":
                local_target = call.peer_of(user_id)
            elif call.direction == "federated_outbound":
                parsed_peer = parse_peer_address_with_policy(call.callee_address, self.settings.tor_enabled)
                relay_data = (parsed_peer.server_onion, call.caller_address, call.callee_address)
            else:
                parsed_peer = parse_peer_address_with_policy(call.caller_address, self.settings.tor_enabled)
                relay_data = (parsed_peer.server_onion, call.callee_address, call.caller_address)

        if local_target is not None:
            await connection_manager.send_to_user(local_target, local_payload)
            return

        if relay_data is not None:
            peer_onion, from_user_address, to_user_address = relay_data
            payload_json = {
                "relay_id": str(uuid4()),
                "from_user_address": from_user_address,
                "to_user_address": to_user_address,
            }
            payload_json.update(federation_payload)
            try:
                await federation_client.post_signed(peer_onion, federation_path, payload_json)
            except FederationClientError as exc:
                raise CallProtocolError(f"{event_type}.relay_failed", exc.detail) from exc

    async def relay_webrtc_offer(self, payload: FederationCallWebRtcOfferRequestV2) -> None:
        self._validate_webrtc_enabled()
        self._validate_call_mode(payload.call_mode, payload.max_participants)
        await self._relay_webrtc_event_to_local(
            call_id=payload.call_id,
            payload={
                "type": "call.webrtc.offer",
                "call_id": payload.call_id,
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
                "from_user_address": payload.from_user_address,
            },
        )

    async def relay_webrtc_answer(self, payload: FederationCallWebRtcAnswerRequestV2) -> None:
        self._validate_webrtc_enabled()
        self._validate_call_mode(payload.call_mode, payload.max_participants)
        await self._relay_webrtc_event_to_local(
            call_id=payload.call_id,
            payload={
                "type": "call.webrtc.answer",
                "call_id": payload.call_id,
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
                "from_user_address": payload.from_user_address,
            },
        )

    async def relay_webrtc_ice(self, payload: FederationCallWebRtcIceRequestV2) -> None:
        self._validate_webrtc_enabled()
        self._validate_call_mode(payload.call_mode, payload.max_participants)
        await self._relay_webrtc_event_to_local(
            call_id=payload.call_id,
            payload={
                "type": "call.webrtc.ice",
                "call_id": payload.call_id,
                "candidate": payload.candidate,
                "sdp_mid": payload.sdp_mid,
                "sdp_mline_index": payload.sdp_mline_index,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
                "from_user_address": payload.from_user_address,
            },
        )

    async def _relay_webrtc_event_to_local(self, call_id: str, payload: dict) -> None:
        local_target: str | None = None
        async with self._lock:
            call = self._calls.get(call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.state != "active":
                raise CallProtocolError("call_not_active", "Call is not active")
            if call.direction == "federated_outbound":
                local_target = call.caller_user_id
            elif call.direction == "federated_inbound":
                local_target = call.callee_user_id
            else:
                raise CallProtocolError("invalid_state", "Local call cannot consume federated WebRTC relay")
        if local_target:
            await connection_manager.send_to_user(local_target, payload)

    async def relay_offer(self, session: AsyncSession, payload: FederationCallOfferRequest) -> None:
        local_user = await self._local_user_for_address(session, payload.to_user_address)

        async with self._lock:
            if local_user.id in self._user_to_call:
                raise CallProtocolError("peer_busy", "Local callee is busy")

        conversation = await conversation_service.get_or_create_remote_for_local_user(
            session,
            local_user,
            payload.from_user_address,
        )
        await session.commit()

        call = CallSession(
            call_id=payload.call_id,
            conversation_id=conversation.id,
            caller_user_id=None,
            callee_user_id=local_user.id,
            caller_address=payload.from_user_address,
            callee_address=payload.to_user_address,
            direction="federated_inbound",
        )

        async with self._lock:
            if local_user.id in self._user_to_call:
                raise CallProtocolError("peer_busy", "Local callee is busy")
            self._calls[payload.call_id] = call
            self._user_to_call[local_user.id] = payload.call_id
            call.timeout_task = asyncio.create_task(self._expire_ringing(payload.call_id))

        await connection_manager.send_to_user(
            local_user.id,
            {
                "type": "call.incoming",
                "call_id": payload.call_id,
                "conversation_id": conversation.id,
                "from_user_id": "",
                "from_user_address": payload.from_user_address,
            },
        )

    async def relay_accept(self, payload: FederationCallAcceptRequest) -> None:
        local_user_id = None
        conversation_id = ""
        peer_address = payload.from_user_address

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.direction != "federated_outbound":
                raise CallProtocolError("invalid_state", "Call is not waiting for remote accept")
            if call.state != "ringing":
                raise CallProtocolError("invalid_state", "Call is not ringing")

            call.state = "active"
            if call.timeout_task is not None:
                call.timeout_task.cancel()
                call.timeout_task = None

            local_user_id = call.caller_user_id
            conversation_id = call.conversation_id

        if local_user_id:
            await connection_manager.send_to_user(
                local_user_id,
                {
                    "type": "call.accepted",
                    "call_id": payload.call_id,
                    "conversation_id": conversation_id,
                    "peer_user_id": "",
                    "peer_user_address": peer_address,
                    **self._webrtc_metadata(),
                },
            )

    async def relay_reject(self, payload: FederationCallRejectRequest) -> None:
        local_user_id = None
        conversation_id = ""

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.direction != "federated_outbound":
                raise CallProtocolError("invalid_state", "Call is not waiting for remote reject")

            local_user_id = call.caller_user_id
            conversation_id = call.conversation_id
            self._remove_call_locked(payload.call_id)

        if local_user_id:
            await connection_manager.send_to_user(
                local_user_id,
                {
                    "type": "call.rejected",
                    "call_id": payload.call_id,
                    "conversation_id": conversation_id,
                    "reason": payload.reason,
                },
            )

    async def relay_end(self, payload: FederationCallEndRequest) -> None:
        targets: list[str] = []
        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                return
            targets = call.local_participants()
            self._remove_call_locked(payload.call_id)

        for user_id in targets:
            await connection_manager.send_to_user(
                user_id,
                {
                    "type": "call.ended",
                    "call_id": payload.call_id,
                    "reason": payload.reason,
                    "by_user_id": "",
                },
            )

    async def relay_audio(self, payload: FederationCallAudioRequest) -> None:
        if not self.settings.enable_legacy_call_audio_ws:
            raise CallProtocolError("audio_deprecated", "WS audio transport is disabled; use WebRTC")
        local_target: str | None = None
        try:
            pcm = base64.b64decode(payload.pcm_b64.encode("utf-8"), validate=True)
        except binascii.Error as exc:
            raise CallProtocolError("invalid_audio", "Invalid audio payload encoding") from exc

        if len(pcm) == 0:
            return
        if len(pcm) > self.settings.voice_audio_max_chunk_bytes:
            raise CallProtocolError("audio_too_large", "Audio chunk too large")

        async with self._lock:
            call = self._calls.get(payload.call_id)
            if call is None:
                raise CallProtocolError("call_not_found", "Call session not found")
            if call.state != "active":
                raise CallProtocolError("call_not_active", "Call is not active")

            now = time.monotonic()
            key = payload.from_user_address.strip().lower()
            previous = call.last_audio_at.get(key)
            min_interval = max(0.001, float(self.settings.voice_audio_min_interval_ms) / 1000.0)
            if previous is not None and (now - previous) < min_interval:
                raise CallProtocolError("audio_rate_limited", "Audio frame rate too high")
            call.last_audio_at[key] = now

            if call.direction == "federated_outbound":
                local_target = call.caller_user_id
            elif call.direction == "federated_inbound":
                local_target = call.callee_user_id
            else:
                raise CallProtocolError("invalid_state", "Local call cannot consume federated audio")

        if local_target is not None:
            await connection_manager.send_to_user(
                local_target,
                {
                    "type": "call.audio",
                    "call_id": payload.call_id,
                    "from_user_id": "",
                    "from_user_address": payload.from_user_address,
                    "sequence": payload.sequence,
                    "pcm_b64": payload.pcm_b64,
                },
            )

    async def handle_disconnect(self, user_id: str) -> None:
        call_id = None
        async with self._lock:
            call_id = self._user_to_call.get(user_id)
        if call_id is None:
            return
        await self._end_call(call_id, "peer_disconnected", by_user_id=user_id)

    async def _expire_ringing(self, call_id: str) -> None:
        try:
            await asyncio.sleep(self.settings.voice_call_ring_timeout_seconds)
            await self._end_call(call_id, "missed", by_user_id=None, require_ringing=True)
        except asyncio.CancelledError:
            return

    async def _end_call(
        self,
        call_id: str,
        reason: str,
        by_user_id: str | None,
        require_ringing: bool = False,
    ) -> None:
        relay_data: tuple[str, str, str] | None = None
        targets: list[str] = []

        async with self._lock:
            call = self._calls.get(call_id)
            if call is None:
                if require_ringing:
                    return
                raise CallProtocolError("call_not_found", "Call session not found")
            if require_ringing and call.state != "ringing":
                return
            if by_user_id is not None and not call.has_participant(by_user_id):
                raise CallProtocolError("forbidden", "Not a participant in this call")

            targets = call.local_participants()
            if call.federated:
                if call.direction == "federated_outbound":
                    parsed_peer = parse_peer_address_with_policy(call.callee_address, self.settings.tor_enabled)
                    relay_data = (parsed_peer.server_onion, call.caller_address, call.callee_address)
                else:
                    parsed_peer = parse_peer_address_with_policy(call.caller_address, self.settings.tor_enabled)
                    relay_data = (parsed_peer.server_onion, call.callee_address, call.caller_address)

            self._remove_call_locked(call_id)

        payload = {
            "type": "call.ended",
            "call_id": call_id,
            "reason": reason,
            "by_user_id": by_user_id,
        }
        for user_id in targets:
            await connection_manager.send_to_user(user_id, payload)

        if relay_data is not None:
            peer_onion, from_user_address, to_user_address = relay_data
            try:
                await federation_client.post_signed(
                    peer_onion,
                    f"{self.settings.api_prefix}/federation/calls/end",
                    {
                        "relay_id": str(uuid4()),
                        "call_id": call_id,
                        "from_user_address": from_user_address,
                        "to_user_address": to_user_address,
                        "reason": reason,
                    },
                )
            except FederationClientError:
                pass

    def _remove_call_locked(self, call_id: str) -> None:
        call = self._calls.pop(call_id, None)
        if call is None:
            return
        for user_id in call.local_participants():
            self._user_to_call.pop(user_id, None)
        current_task = asyncio.current_task()
        if call.timeout_task is not None and call.timeout_task is not current_task:
            call.timeout_task.cancel()
        call.timeout_task = None


call_service = CallService()
