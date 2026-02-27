from __future__ import annotations

import asyncio
import base64
import binascii
import time
from datetime import UTC, datetime, timedelta
from uuid import uuid4

from fastapi import HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.conversation_member import ConversationMember
from app.models.group_call_participant import GroupCallParticipant
from app.models.group_call_session import GroupCallSession
from app.models.user import User
from app.schemas.call import CallAudioRequest
from app.schemas.v2_federation import (
    FederationGroupCallEndRequestV2,
    FederationGroupCallJoinRequestV2,
    FederationGroupCallLeaveRequestV2,
    FederationGroupCallOfferRequestV2,
    FederationGroupCallWebRtcAnswerRequestV2,
    FederationGroupCallWebRtcIceRequestV2,
    FederationGroupCallWebRtcOfferRequestV2,
)
from app.services.federation_client import FederationClientError, federation_client
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_authority import is_local_server_authority
from app.services.server_identity import get_server_onion, server_address_for_username
from app.ws.manager import connection_manager


class GroupCallService:
    def __init__(self) -> None:
        self.settings = get_settings()
        self._audio_last_at: dict[tuple[str, str], float] = {}

    @staticmethod
    def _delivery_states() -> set[str]:
        return {"joined", "ringing", "offline_pending"}

    def _clear_audio_state_for_call(self, call_id: str) -> None:
        stale_keys = [key for key in self._audio_last_at if key[0] == call_id]
        for key in stale_keys:
            self._audio_last_at.pop(key, None)

    async def _require_enabled(self) -> None:
        if not self.settings.enable_group_call_v2c:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group calls are disabled")

    async def _user(self, session: AsyncSession, user_id: str) -> User:
        stmt = select(User).where(User.id == user_id, User.disabled_at.is_(None))
        user = (await session.execute(stmt)).scalar_one_or_none()
        if user is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="User not found")
        return user

    async def _call(self, session: AsyncSession, call_id: str) -> GroupCallSession:
        stmt = select(GroupCallSession).where(GroupCallSession.call_id == call_id)
        row = (await session.execute(stmt)).scalar_one_or_none()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Call not found")
        return row

    async def _participants(self, session: AsyncSession, call_id: str) -> list[GroupCallParticipant]:
        stmt = select(GroupCallParticipant).where(GroupCallParticipant.call_id == call_id)
        return list((await session.execute(stmt)).scalars().all())

    async def is_group_call(self, session: AsyncSession, call_id: str) -> bool:
        stmt = select(GroupCallSession.call_id).where(GroupCallSession.call_id == call_id).limit(1)
        return (await session.execute(stmt)).scalar_one_or_none() is not None

    @staticmethod
    def _participant_summary(rows: list[GroupCallParticipant]) -> list[dict]:
        return [{"member_address": row.member_address, "state": row.state} for row in rows]

    async def _post_federation(
        self,
        *,
        peer_onion: str,
        endpoint_path: str,
        payload_json: dict,
    ) -> None:
        try:
            await federation_client.post_signed(peer_onion, endpoint_path, payload_json)
        except FederationClientError:
            await metrics.inc("group_calls.signal.relay_failed")

    def _remote_servers_for_participants(self, rows: list[GroupCallParticipant]) -> set[str]:
        remote_servers: set[str] = set()
        for row in rows:
            try:
                parsed = parse_peer_address_with_policy(row.member_address, self.settings.tor_enabled)
            except ValueError:
                continue
            if is_local_server_authority(parsed.server_onion, self.settings):
                continue
            remote_servers.add(parsed.server_onion)
        return remote_servers

    async def _notify_call_state(self, session: AsyncSession, call: GroupCallSession) -> None:
        participants = await self._participants(session, call.call_id)
        payload = {
            "type": "call.group.state",
            "call_id": call.call_id,
            "conversation_id": call.conversation_id,
            "group_uid": call.group_uid,
            "state": call.state,
            "participants": self._participant_summary(participants),
            "call_mode": "webrtc",
            "max_participants": self.settings.group_call_max_participants,
        }
        for row in participants:
            if row.local_user_id and row.state in self._delivery_states():
                await connection_manager.send_to_user(row.local_user_id, payload)

    async def _notify_participant_event(
        self,
        session: AsyncSession,
        call: GroupCallSession,
        member_address: str,
        member_state: str,
    ) -> None:
        participants = await self._participants(session, call.call_id)
        payload = {
            "type": "call.group.participant",
            "call_id": call.call_id,
            "conversation_id": call.conversation_id,
            "group_uid": call.group_uid,
            "member_address": member_address,
            "state": member_state,
        }
        for row in participants:
            if row.local_user_id and row.state in self._delivery_states():
                await connection_manager.send_to_user(row.local_user_id, payload)

    async def offer(self, session: AsyncSession, caller_user_id: str, conversation_id: str) -> GroupCallSession:
        await self._require_enabled()
        caller = await self._user(session, caller_user_id)
        caller_address = server_address_for_username(caller.username)
        conversation = (await session.execute(select(Conversation).where(Conversation.id == conversation_id))).scalar_one_or_none()
        if conversation is None or conversation.conversation_type != "group":
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group conversation not found")
        member_rows = list(
            (
                await session.execute(
                    select(ConversationMember).where(
                        ConversationMember.conversation_id == conversation.id,
                        ConversationMember.status == "active",
                    )
                )
            ).scalars()
        )
        caller_member = next((row for row in member_rows if row.member_user_id == caller_user_id), None)
        if caller_member is None:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a group member")
        if len(member_rows) > self.settings.group_call_max_participants:
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Too many active group members for mesh call")

        existing_call = (
            await session.execute(
                select(GroupCallSession)
                .where(
                    GroupCallSession.conversation_id == conversation.id,
                    GroupCallSession.state.in_(["ringing", "active"]),
                )
                .order_by(GroupCallSession.started_at.desc())
                .limit(1)
            )
        ).scalar_one_or_none()
        if existing_call is not None:
            now = datetime.now(UTC)
            existing_participants = await self._participants(session, existing_call.call_id)
            caller_participant = next(
                (row for row in existing_participants if row.member_address == caller_address),
                None,
            )
            if caller_participant is None:
                session.add(
                    GroupCallParticipant(
                        call_id=existing_call.call_id,
                        member_address=caller_address,
                        local_user_id=caller_user_id,
                        state="joined",
                        invited_at=now,
                        joined_at=now,
                        last_signal_at=now,
                    )
                )
            else:
                caller_participant.state = "joined"
                caller_participant.joined_at = now
                caller_participant.left_at = None
                caller_participant.last_signal_at = now
            existing_call.state = "active"
            await session.commit()
            await metrics.inc("group_calls.joined")
            await self._notify_call_state(session, existing_call)
            await self._notify_participant_event(session, existing_call, caller_address, "joined")
            participants_after = await self._participants(session, existing_call.call_id)
            for peer_onion in self._remote_servers_for_participants(participants_after):
                await self._post_federation(
                    peer_onion=peer_onion,
                    endpoint_path="/api/v2/federation/group-calls/join",
                    payload_json={
                        "relay_id": str(uuid4()),
                        "call_id": existing_call.call_id,
                        "group_uid": existing_call.group_uid,
                        "member_address": caller_address,
                    },
                )
            return existing_call

        remote_servers = {
            row.member_server_onion
            for row in member_rows
            if row.member_server_onion and not is_local_server_authority(row.member_server_onion, self.settings)
        }

        now = datetime.now(UTC)
        call = GroupCallSession(
            call_id=str(uuid4()),
            conversation_id=conversation.id,
            group_uid=conversation.group_uid or "",
            initiator_address=caller_address,
            state="ringing",
            started_at=now,
            ring_expires_at=now + timedelta(seconds=self.settings.group_call_ring_ttl_seconds),
        )
        session.add(call)
        await session.flush()

        for member in member_rows:
            participant_state = "joined" if member.member_address == caller_address else "ringing"
            if member.member_user_id and participant_state == "ringing":
                if not await connection_manager.has_user(member.member_user_id):
                    participant_state = "offline_pending"
                    await metrics.inc("group_calls.offline_pending")
            session.add(
                GroupCallParticipant(
                    call_id=call.call_id,
                    member_address=member.member_address,
                    local_user_id=member.member_user_id,
                    state=participant_state,
                    invited_at=now,
                    joined_at=now if participant_state == "joined" else None,
                )
            )

        await session.commit()
        await metrics.inc("group_calls.created")
        await self._notify_call_state(session, call)

        participants = await self._participants(session, call.call_id)
        for row in participants:
            if row.local_user_id and row.member_address != caller_address:
                await connection_manager.send_to_user(
                    row.local_user_id,
                    {
                        "type": "call.group.incoming",
                        "call_id": call.call_id,
                        "conversation_id": conversation.id,
                        "group_uid": call.group_uid,
                        "from_user_address": caller_address,
                        "call_mode": "webrtc",
                        "max_participants": self.settings.group_call_max_participants,
                    },
                )
        for peer_onion in remote_servers:
            await self._post_federation(
                peer_onion=peer_onion,
                endpoint_path="/api/v2/federation/group-calls/offer",
                payload_json={
                    "relay_id": str(uuid4()),
                    "call_id": call.call_id,
                    "group_uid": call.group_uid,
                    "conversation_id": call.conversation_id,
                    "from_user_address": caller_address,
                    "call_schema_version": 1,
                    "call_mode": "webrtc",
                    "max_participants": self.settings.group_call_max_participants,
                    "ring_expires_at": call.ring_expires_at.isoformat(),
                },
            )
        return call

    async def join(self, session: AsyncSession, user_id: str, call_id: str) -> GroupCallSession:
        await self._require_enabled()
        user = await self._user(session, user_id)
        address = server_address_for_username(user.username)
        call = await self._call(session, call_id)
        row = (
            await session.execute(
                select(GroupCallParticipant).where(
                    GroupCallParticipant.call_id == call.call_id,
                    GroupCallParticipant.member_address == address,
                )
            )
        ).scalar_one_or_none()
        if row is None:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a call participant")
        now = datetime.now(UTC)
        row.state = "joined"
        row.joined_at = now
        row.left_at = None
        row.last_signal_at = now
        call.state = "active"
        await session.commit()
        await metrics.inc("group_calls.joined")
        await self._notify_call_state(session, call)
        await self._notify_participant_event(session, call, address, "joined")
        participants = await self._participants(session, call.call_id)
        for peer_onion in self._remote_servers_for_participants(participants):
            await self._post_federation(
                peer_onion=peer_onion,
                endpoint_path="/api/v2/federation/group-calls/join",
                payload_json={
                    "relay_id": str(uuid4()),
                    "call_id": call.call_id,
                    "group_uid": call.group_uid,
                    "member_address": address,
                },
            )
        return call

    async def reject(self, session: AsyncSession, user_id: str, call_id: str) -> GroupCallSession:
        await self._require_enabled()
        user = await self._user(session, user_id)
        address = server_address_for_username(user.username)
        call = await self._call(session, call_id)
        row = (
            await session.execute(
                select(GroupCallParticipant).where(
                    GroupCallParticipant.call_id == call.call_id,
                    GroupCallParticipant.member_address == address,
                )
            )
        ).scalar_one_or_none()
        if row is None:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a call participant")
        row.state = "declined"
        row.left_at = datetime.now(UTC)
        await session.commit()
        if row.local_user_id:
            await connection_manager.send_to_user(
                row.local_user_id,
                {"type": "call.group.ended", "call_id": call.call_id, "reason": "declined"},
            )
        await self._notify_call_state(session, call)
        await self._notify_participant_event(session, call, address, "declined")
        participants = await self._participants(session, call.call_id)
        for peer_onion in self._remote_servers_for_participants(participants):
            await self._post_federation(
                peer_onion=peer_onion,
                endpoint_path="/api/v2/federation/group-calls/leave",
                payload_json={
                    "relay_id": str(uuid4()),
                    "call_id": call.call_id,
                    "group_uid": call.group_uid,
                    "member_address": address,
                    "reason": "declined",
                },
            )
        return call

    async def leave(self, session: AsyncSession, user_id: str, call_id: str, reason: str = "left") -> GroupCallSession:
        await self._require_enabled()
        user = await self._user(session, user_id)
        address = server_address_for_username(user.username)
        call = await self._call(session, call_id)
        row = (
            await session.execute(
                select(GroupCallParticipant).where(
                    GroupCallParticipant.call_id == call.call_id,
                    GroupCallParticipant.member_address == address,
                )
            )
        ).scalar_one_or_none()
        if row is None:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a call participant")
        now = datetime.now(UTC)
        row.state = "left"
        row.left_at = now
        row.last_signal_at = now
        participants = await self._participants(session, call.call_id)
        active_remaining = [
            item for item in participants if item.member_address != address and item.state in {"joined", "ringing", "offline_pending"}
        ]
        if not active_remaining:
            call.state = "ended"
            call.ended_at = now
            self._clear_audio_state_for_call(call.call_id)
            await metrics.inc("group_calls.ended")
        await session.commit()
        await metrics.inc("group_calls.left")

        if row.local_user_id:
            await connection_manager.send_to_user(
                row.local_user_id,
                {"type": "call.group.ended", "call_id": call.call_id, "reason": reason or "left"},
            )

        await self._notify_call_state(session, call)
        await self._notify_participant_event(session, call, address, "left")
        remote_servers = self._remote_servers_for_participants(participants)
        for peer_onion in remote_servers:
            await self._post_federation(
                peer_onion=peer_onion,
                endpoint_path="/api/v2/federation/group-calls/leave",
                payload_json={
                    "relay_id": str(uuid4()),
                    "call_id": call.call_id,
                    "group_uid": call.group_uid,
                    "member_address": address,
                    "reason": reason,
                },
            )
        if call.state == "ended":
            for participant in participants:
                if participant.local_user_id:
                    await connection_manager.send_to_user(
                        participant.local_user_id,
                        {"type": "call.group.ended", "call_id": call.call_id, "reason": reason},
                    )
            for peer_onion in remote_servers:
                await self._post_federation(
                    peer_onion=peer_onion,
                    endpoint_path="/api/v2/federation/group-calls/end",
                    payload_json={
                        "relay_id": str(uuid4()),
                        "call_id": call.call_id,
                        "group_uid": call.group_uid,
                        "reason": reason,
                    },
                )
        return call

    async def handle_disconnect(self, session: AsyncSession, user_id: str) -> None:
        if not self.settings.enable_group_call_v2c:
            return
        joined_stmt = (
            select(GroupCallParticipant.call_id)
            .where(
                GroupCallParticipant.local_user_id == user_id,
                GroupCallParticipant.state == "joined",
            )
            .distinct()
        )
        joined_call_ids = [str(item) for item in (await session.execute(joined_stmt)).scalars().all()]
        for call_id in joined_call_ids:
            try:
                await self.leave(session, user_id, call_id, reason="peer_disconnected")
            except HTTPException:
                continue

        now = datetime.now(UTC)
        ringing_rows = list(
            (
                await session.execute(
                    select(GroupCallParticipant, GroupCallSession)
                    .join(GroupCallSession, GroupCallSession.call_id == GroupCallParticipant.call_id)
                    .where(
                        GroupCallParticipant.local_user_id == user_id,
                        GroupCallParticipant.state.in_(["ringing", "offline_pending"]),
                        GroupCallSession.state == "ringing",
                    )
                )
            ).all()
        )
        touched_calls: set[str] = set()
        for participant, _ in ringing_rows:
            participant.state = "offline_pending"
            participant.last_signal_at = now
            touched_calls.add(participant.call_id)
        if ringing_rows:
            await session.commit()
            for call_id in touched_calls:
                try:
                    call = await self._call(session, call_id)
                except HTTPException:
                    continue
                await self._notify_call_state(session, call)

    async def replay_pending_for_user(self, session: AsyncSession, user_id: str) -> None:
        await self._require_enabled()
        user = await self._user(session, user_id)
        address = server_address_for_username(user.username)
        now = datetime.now(UTC)
        rows = list(
            (
                await session.execute(
                    select(GroupCallParticipant, GroupCallSession)
                    .join(GroupCallSession, GroupCallSession.call_id == GroupCallParticipant.call_id)
                    .where(
                        GroupCallParticipant.member_address == address,
                        GroupCallParticipant.local_user_id == user_id,
                        GroupCallParticipant.state == "offline_pending",
                        GroupCallSession.state == "ringing",
                        GroupCallSession.ring_expires_at > now,
                    )
                )
            ).all()
        )
        for participant, call in rows:
            participant.state = "ringing"
            await connection_manager.send_to_user(
                user_id,
                {
                    "type": "call.group.incoming",
                    "call_id": call.call_id,
                    "conversation_id": call.conversation_id,
                    "group_uid": call.group_uid,
                    "from_user_address": call.initiator_address,
                    "call_mode": "webrtc",
                    "max_participants": self.settings.group_call_max_participants,
                },
            )
        if rows:
            await session.commit()

    async def route_webrtc_signal(
        self,
        session: AsyncSession,
        *,
        sender_user_id: str,
        call_id: str,
        event_type: str,
        body: dict,
        target_user_address: str,
    ) -> None:
        await self._require_enabled()
        call = await self._call(session, call_id)
        sender_user = await self._user(session, sender_user_id)
        sender_address = server_address_for_username(sender_user.username)
        participants = await self._participants(session, call_id)
        sender_participant = next((item for item in participants if item.member_address == sender_address), None)
        if sender_participant is None:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a call participant")
        sender_participant.last_signal_at = datetime.now(UTC)
        target = target_user_address.strip().lower()
        target_participant = next((item for item in participants if item.member_address == target), None)
        if target_participant is None:
            await metrics.inc("group_calls.signal.rejected")
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Target is not in this call")
        payload = dict(body)
        payload["type"] = event_type
        payload["call_id"] = call_id
        payload["source_user_address"] = sender_address
        payload["target_user_address"] = target
        if target_participant.local_user_id:
            await connection_manager.send_to_user(target_participant.local_user_id, payload)
            return
        parsed = parse_peer_address_with_policy(target, self.settings.tor_enabled)
        if is_local_server_authority(parsed.server_onion, self.settings):
            await metrics.inc("group_calls.signal.rejected")
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Target user is unavailable")
        endpoint_path = ""
        federation_payload: dict = {
            "relay_id": str(uuid4()),
            "call_id": call.call_id,
            "group_uid": call.group_uid,
            "source_user_address": sender_address,
            "target_user_address": target,
            "call_schema_version": int(body.get("call_schema_version", 1)),
            "call_mode": str(body.get("call_mode", "webrtc")),
            "max_participants": int(body.get("max_participants", self.settings.group_call_max_participants)),
        }
        if event_type == "call.webrtc.offer":
            endpoint_path = "/api/v2/federation/group-calls/webrtc-offer"
            federation_payload["sdp"] = str(body.get("sdp", ""))
        elif event_type == "call.webrtc.answer":
            endpoint_path = "/api/v2/federation/group-calls/webrtc-answer"
            federation_payload["sdp"] = str(body.get("sdp", ""))
        elif event_type == "call.webrtc.ice":
            endpoint_path = "/api/v2/federation/group-calls/webrtc-ice"
            federation_payload["candidate"] = str(body.get("candidate", ""))
            federation_payload["sdp_mid"] = body.get("sdp_mid")
            federation_payload["sdp_mline_index"] = body.get("sdp_mline_index")
        else:
            await metrics.inc("group_calls.signal.rejected")
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Unsupported signaling event")
        try:
            await federation_client.post_signed(parsed.server_onion, endpoint_path, federation_payload)
        except FederationClientError as exc:
            await metrics.inc("group_calls.signal.relay_failed")
            raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=exc.detail) from exc

    async def relay_offer(self, session: AsyncSession, payload: FederationGroupCallOfferRequestV2) -> None:
        await self._require_enabled()
        existing = (await session.execute(select(GroupCallSession).where(GroupCallSession.call_id == payload.call_id))).scalar_one_or_none()
        if existing is not None:
            return
        conversation = (await session.execute(select(Conversation).where(Conversation.group_uid == payload.group_uid))).scalar_one_or_none()
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group not found")
        parsed = parse_peer_address_with_policy(payload.from_user_address, self.settings.tor_enabled)
        if is_local_server_authority(parsed.server_onion, self.settings):
            return
        call = GroupCallSession(
            call_id=payload.call_id,
            conversation_id=conversation.id,
            group_uid=payload.group_uid,
            initiator_address=payload.from_user_address,
            state="ringing",
            ring_expires_at=datetime.fromisoformat(payload.ring_expires_at.replace("Z", "+00:00")),
            started_at=datetime.now(UTC),
        )
        session.add(call)
        members = list(
            (
                await session.execute(
                    select(ConversationMember).where(
                        ConversationMember.conversation_id == conversation.id,
                        ConversationMember.status == "active",
                    )
                )
            ).scalars()
        )
        for member in members:
            state_value = "joined" if member.member_address == payload.from_user_address else "ringing"
            if member.member_user_id and state_value == "ringing" and not await connection_manager.has_user(member.member_user_id):
                state_value = "offline_pending"
            session.add(
                GroupCallParticipant(
                    call_id=call.call_id,
                    member_address=member.member_address,
                    local_user_id=member.member_user_id,
                    state=state_value,
                    invited_at=datetime.now(UTC),
                )
            )
        await session.commit()
        await self._notify_call_state(session, call)
        participants = await self._participants(session, call.call_id)
        for row in participants:
            if row.local_user_id and row.member_address != payload.from_user_address:
                await connection_manager.send_to_user(
                    row.local_user_id,
                    {
                        "type": "call.group.incoming",
                        "call_id": call.call_id,
                        "conversation_id": conversation.id,
                        "group_uid": payload.group_uid,
                        "from_user_address": payload.from_user_address,
                        "call_mode": "webrtc",
                        "max_participants": self.settings.group_call_max_participants,
                    },
                )

    async def relay_join(self, session: AsyncSession, payload: FederationGroupCallJoinRequestV2) -> None:
        await self._require_enabled()
        call = await self._call(session, payload.call_id)
        participant = (
            await session.execute(
                select(GroupCallParticipant).where(
                    GroupCallParticipant.call_id == call.call_id,
                    GroupCallParticipant.member_address == payload.member_address,
                )
            )
        ).scalar_one_or_none()
        if participant is None:
            return
        participant.state = "joined"
        participant.joined_at = datetime.now(UTC)
        call.state = "active"
        await session.commit()
        await self._notify_call_state(session, call)
        await self._notify_participant_event(session, call, payload.member_address, "joined")

    async def relay_leave(self, session: AsyncSession, payload: FederationGroupCallLeaveRequestV2) -> None:
        await self._require_enabled()
        call = await self._call(session, payload.call_id)
        participant = (
            await session.execute(
                select(GroupCallParticipant).where(
                    GroupCallParticipant.call_id == call.call_id,
                    GroupCallParticipant.member_address == payload.member_address,
                )
            )
        ).scalar_one_or_none()
        if participant is None:
            return
        participant.state = "left"
        participant.left_at = datetime.now(UTC)
        participants = await self._participants(session, call.call_id)
        remaining = [row for row in participants if row.member_address != payload.member_address and row.state in {"joined", "ringing", "offline_pending"}]
        if not remaining:
            call.state = "ended"
            call.ended_at = datetime.now(UTC)
            self._clear_audio_state_for_call(call.call_id)
        await session.commit()
        await self._notify_call_state(session, call)
        await self._notify_participant_event(session, call, payload.member_address, "left")
        if call.state == "ended":
            for row in participants:
                if row.local_user_id:
                    await connection_manager.send_to_user(
                        row.local_user_id,
                        {"type": "call.group.ended", "call_id": call.call_id, "reason": payload.reason},
                    )

    async def relay_end(self, session: AsyncSession, payload: FederationGroupCallEndRequestV2) -> None:
        await self._require_enabled()
        call = await self._call(session, payload.call_id)
        call.state = "ended"
        call.ended_at = datetime.now(UTC)
        self._clear_audio_state_for_call(call.call_id)
        await session.commit()
        rows = await self._participants(session, call.call_id)
        for row in rows:
            if row.local_user_id:
                await connection_manager.send_to_user(
                    row.local_user_id,
                    {"type": "call.group.ended", "call_id": call.call_id, "reason": payload.reason},
                )

    async def audio(self, session: AsyncSession, user_id: str, payload: CallAudioRequest) -> None:
        await self._require_enabled()
        if not self.settings.enable_legacy_call_audio_ws:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="WS audio transport is disabled")
        try:
            pcm = base64.b64decode(payload.pcm_b64.encode("utf-8"), validate=True)
        except binascii.Error as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid audio payload encoding") from exc
        if not pcm:
            return
        if len(pcm) > self.settings.voice_audio_max_chunk_bytes:
            raise HTTPException(status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE, detail="Audio chunk too large")

        call = await self._call(session, payload.call_id)
        if call.state != "active":
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Call is not active")

        sender_participant = (
            await session.execute(
                select(GroupCallParticipant).where(
                    GroupCallParticipant.call_id == call.call_id,
                    GroupCallParticipant.local_user_id == user_id,
                )
            )
        ).scalar_one_or_none()
        if sender_participant is None or sender_participant.state != "joined":
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not an active call participant")
        sender_address = sender_participant.member_address

        now_mono = time.monotonic()
        min_interval = max(0.001, float(self.settings.voice_audio_min_interval_ms) / 1000.0)
        rate_key = (call.call_id, sender_address)
        previous = self._audio_last_at.get(rate_key)
        if previous is not None and (now_mono - previous) < min_interval:
            return
        self._audio_last_at[rate_key] = now_mono

        recipients = list(
            (
                await session.execute(
                    select(GroupCallParticipant.local_user_id).where(
                        GroupCallParticipant.call_id == call.call_id,
                        GroupCallParticipant.state == "joined",
                        GroupCallParticipant.member_address != sender_address,
                        GroupCallParticipant.local_user_id.is_not(None),
                    )
                )
            ).scalars()
        )
        out_payload = {
            "type": "call.audio",
            "call_id": call.call_id,
            "from_user_id": user_id,
            "from_user_address": sender_address,
            "sequence": payload.sequence,
            "pcm_b64": payload.pcm_b64,
        }
        if recipients:
            await asyncio.gather(
                *(connection_manager.send_to_user(recipient_user_id, out_payload) for recipient_user_id in recipients),
                return_exceptions=True,
            )

    async def _relay_webrtc_to_local_target(
        self,
        session: AsyncSession,
        *,
        call_id: str,
        source_user_address: str,
        target_user_address: str,
        event_type: str,
        payload: dict,
    ) -> None:
        await self._require_enabled()
        call = await self._call(session, call_id)
        participants = await self._participants(session, call.call_id)
        source = source_user_address.strip().lower()
        target = target_user_address.strip().lower()
        source_participant = next((item for item in participants if item.member_address == source), None)
        target_participant = next((item for item in participants if item.member_address == target), None)
        if source_participant is None or target_participant is None:
            await metrics.inc("group_calls.signal.rejected")
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid signaling participants")
        if not target_participant.local_user_id:
            await metrics.inc("group_calls.signal.rejected")
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Target user is unavailable")
        outgoing = dict(payload)
        outgoing["type"] = event_type
        outgoing["call_id"] = call_id
        outgoing["source_user_address"] = source
        outgoing["target_user_address"] = target
        await connection_manager.send_to_user(target_participant.local_user_id, outgoing)

    async def relay_webrtc_offer(self, session: AsyncSession, payload: FederationGroupCallWebRtcOfferRequestV2) -> None:
        await self._relay_webrtc_to_local_target(
            session,
            call_id=payload.call_id,
            source_user_address=payload.source_user_address,
            target_user_address=payload.target_user_address,
            event_type="call.webrtc.offer",
            payload={
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
        )

    async def relay_webrtc_answer(self, session: AsyncSession, payload: FederationGroupCallWebRtcAnswerRequestV2) -> None:
        await self._relay_webrtc_to_local_target(
            session,
            call_id=payload.call_id,
            source_user_address=payload.source_user_address,
            target_user_address=payload.target_user_address,
            event_type="call.webrtc.answer",
            payload={
                "sdp": payload.sdp,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
        )

    async def relay_webrtc_ice(self, session: AsyncSession, payload: FederationGroupCallWebRtcIceRequestV2) -> None:
        await self._relay_webrtc_to_local_target(
            session,
            call_id=payload.call_id,
            source_user_address=payload.source_user_address,
            target_user_address=payload.target_user_address,
            event_type="call.webrtc.ice",
            payload={
                "candidate": payload.candidate,
                "sdp_mid": payload.sdp_mid,
                "sdp_mline_index": payload.sdp_mline_index,
                "call_schema_version": payload.call_schema_version,
                "call_mode": payload.call_mode,
                "max_participants": payload.max_participants,
            },
        )


group_call_service = GroupCallService()
