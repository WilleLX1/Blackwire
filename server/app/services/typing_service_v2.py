from __future__ import annotations

from datetime import UTC, datetime
from uuid import uuid4

from fastapi import HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.conversation_member import ConversationMember
from app.models.user import User
from app.schemas.v2_federation import FederationConversationTypingRelayRequestV2
from app.services.conversation_service import conversation_service
from app.services.federation_outbox_service import federation_outbox_service
from app.services.group_conversation_service import group_conversation_service
from app.services.server_authority import is_local_server_authority
from app.services.server_identity import server_address_for_username
from app.ws.manager import connection_manager


class TypingServiceV2:
    def __init__(self) -> None:
        self.settings = get_settings()

    def _ensure_enabled(self) -> None:
        if not self.settings.enable_typing_v03b:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Typing indicators are disabled")

    async def _active_group_members(self, session: AsyncSession, conversation_id: str) -> list[ConversationMember]:
        stmt = select(ConversationMember).where(
            ConversationMember.conversation_id == conversation_id,
            ConversationMember.status == "active",
        )
        return list((await session.execute(stmt)).scalars().all())

    async def _local_recipient_user_ids(
        self,
        session: AsyncSession,
        conversation: Conversation,
        *,
        sender_user_id: str | None,
        sender_address: str,
    ) -> set[str]:
        if conversation.conversation_type == "group":
            members = await self._active_group_members(session, conversation.id)
            recipient_user_ids = {
                row.member_user_id
                for row in members
                if row.member_user_id is not None and row.member_address != sender_address
            }
            return {row for row in recipient_user_ids if row is not None}

        if conversation.kind == "local":
            if sender_user_id is None:
                return set()
            conversation_service.ensure_membership(conversation, sender_user_id)
            recipients: set[str] = set()
            if conversation.user_a_id and conversation.user_a_id != sender_user_id:
                recipients.add(conversation.user_a_id)
            if conversation.user_b_id and conversation.user_b_id != sender_user_id:
                recipients.add(conversation.user_b_id)
            return recipients

        if sender_user_id is not None:
            conversation_service.ensure_membership(conversation, sender_user_id)
        if conversation.local_user_id and sender_address != conversation.peer_address:
            return {conversation.local_user_id}
        return set()

    async def _remote_peer_servers_for_typing(
        self,
        session: AsyncSession,
        conversation: Conversation,
        *,
        sender_user_id: str,
        sender_address: str,
    ) -> set[str]:
        remote_servers: set[str] = set()
        if conversation.conversation_type == "group":
            member = await group_conversation_service.ensure_member_access(
                session,
                conversation,
                sender_user_id,
                require_active=True,
            )
            if member.member_address != sender_address:
                raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Invalid typing sender")
            members = await self._active_group_members(session, conversation.id)
            for row in members:
                server_onion = (row.member_server_onion or "").strip().lower()
                if not server_onion or is_local_server_authority(server_onion, self.settings):
                    continue
                remote_servers.add(server_onion)
            return remote_servers

        conversation_service.ensure_membership(conversation, sender_user_id)
        if conversation.kind == "remote":
            peer_onion = (conversation.peer_server_onion or "").strip().lower()
            if peer_onion and not is_local_server_authority(peer_onion, self.settings):
                remote_servers.add(peer_onion)
        return remote_servers

    async def _fanout_remote(
        self,
        session: AsyncSession,
        *,
        remote_servers: set[str],
        payload: dict,
        sender_key: str,
    ) -> None:
        for peer_onion in remote_servers:
            outbox_item = await federation_outbox_service.enqueue(
                session,
                peer_onion=peer_onion,
                event_type="conversation.typing",
                endpoint_path="/api/v2/federation/conversations/typing",
                payload_json=payload,
                dedupe_key=f"conversation-typing:{sender_key}:{peer_onion}:{payload['relay_id']}",
            )
            await session.commit()
            await federation_outbox_service.deliver_item(session, outbox_item.id)

    async def publish_local_typing(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        sender_user: User,
        state: str,
    ) -> int:
        self._ensure_enabled()
        normalized_state = state.strip().lower()
        if normalized_state not in {"on", "off"}:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="state must be on or off")

        sender_address = server_address_for_username(sender_user.username)
        local_recipient_user_ids = await self._local_recipient_user_ids(
            session,
            conversation,
            sender_user_id=sender_user.id,
            sender_address=sender_address,
        )
        remote_servers = await self._remote_peer_servers_for_typing(
            session,
            conversation,
            sender_user_id=sender_user.id,
            sender_address=sender_address,
        )

        expires_in_ms = int(self.settings.typing_indicator_ttl_seconds * 1000)
        event_payload = {
            "type": "conversation.typing",
            "conversation_id": conversation.id,
            "from_user_address": sender_address,
            "state": normalized_state,
            "expires_in_ms": expires_in_ms,
            "sent_at": datetime.now(UTC).isoformat(),
        }
        for user_id in local_recipient_user_ids:
            await connection_manager.send_to_user(user_id, event_payload)

        relay_payload = {
            "relay_id": str(uuid4()),
            "conversation_id": conversation.id,
            "from_user_address": sender_address,
            "state": normalized_state,
            "expires_in_ms": expires_in_ms,
            "sent_at": event_payload["sent_at"],
        }
        await self._fanout_remote(
            session,
            remote_servers=remote_servers,
            payload=relay_payload,
            sender_key=f"{conversation.id}:{sender_address}:{normalized_state}",
        )
        return expires_in_ms

    async def relay_typing_from_federation(
        self,
        session: AsyncSession,
        payload: FederationConversationTypingRelayRequestV2,
    ) -> None:
        self._ensure_enabled()
        conversation = await conversation_service.get_by_id(session, payload.conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")

        state = payload.state.strip().lower()
        if state not in {"on", "off"}:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="state must be on or off")

        sender_address = payload.from_user_address.strip().lower()
        recipients: set[str] = set()
        if conversation.conversation_type == "group":
            members = await self._active_group_members(session, conversation.id)
            member_addresses = {row.member_address for row in members}
            if sender_address not in member_addresses:
                raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Typing sender is not a group member")
            recipients = {
                row.member_user_id
                for row in members
                if row.member_user_id is not None and row.member_address != sender_address
            }
        elif conversation.kind == "remote":
            if sender_address != (conversation.peer_address or "").strip().lower():
                raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Typing sender mismatch")
            if conversation.local_user_id:
                recipients.add(conversation.local_user_id)

        event_payload = {
            "type": "conversation.typing",
            "conversation_id": conversation.id,
            "from_user_address": sender_address,
            "state": state,
            "expires_in_ms": payload.expires_in_ms,
            "sent_at": payload.sent_at,
        }
        for user_id in recipients:
            await connection_manager.send_to_user(user_id, event_payload)


typing_service_v2 = TypingServiceV2()
