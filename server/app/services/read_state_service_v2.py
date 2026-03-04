from __future__ import annotations

from datetime import UTC, datetime
from uuid import uuid4

from fastapi import HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.conversation_member import ConversationMember
from app.models.conversation_read_cursor import ConversationReadCursor
from app.models.message_event import MessageEvent
from app.models.user import User
from app.schemas.v2_conversation import (
    ConversationReadCursorEntryOutV2,
    ConversationReadCursorOutV2,
    ConversationReadStateOutV2,
)
from app.schemas.v2_federation import FederationConversationReadRelayRequestV2
from app.services.conversation_service import conversation_service
from app.services.federation_outbox_service import federation_outbox_service
from app.services.group_conversation_service import group_conversation_service
from app.services.server_authority import is_local_server_authority
from app.services.server_identity import server_address_for_username
from app.ws.manager import connection_manager


class ReadStateServiceV2:
    def __init__(self) -> None:
        self.settings = get_settings()

    def _ensure_enabled(self) -> None:
        if not self.settings.enable_read_cursor_v03b:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Read cursor is disabled")

    async def _active_group_members(self, session: AsyncSession, conversation_id: str) -> list[ConversationMember]:
        stmt = select(ConversationMember).where(
            ConversationMember.conversation_id == conversation_id,
            ConversationMember.status == "active",
        )
        return list((await session.execute(stmt)).scalars().all())

    async def _ensure_read_access(self, session: AsyncSession, conversation: Conversation, user_id: str) -> None:
        if conversation.conversation_type == "group":
            await group_conversation_service.ensure_member_access(
                session,
                conversation,
                user_id,
                require_active=True,
            )
            return
        conversation_service.ensure_membership(conversation, user_id)

    async def _resolve_local_recipients(
        self,
        session: AsyncSession,
        conversation: Conversation,
    ) -> set[str]:
        if conversation.conversation_type == "group":
            members = await self._active_group_members(session, conversation.id)
            return {row.member_user_id for row in members if row.member_user_id is not None}

        if conversation.kind == "local":
            recipients: set[str] = set()
            if conversation.user_a_id:
                recipients.add(conversation.user_a_id)
            if conversation.user_b_id:
                recipients.add(conversation.user_b_id)
            return recipients

        if conversation.local_user_id:
            return {conversation.local_user_id}
        return set()

    async def _resolve_remote_peer_servers(
        self,
        session: AsyncSession,
        conversation: Conversation,
    ) -> set[str]:
        remote_servers: set[str] = set()
        if conversation.conversation_type == "group":
            members = await self._active_group_members(session, conversation.id)
            for member in members:
                server_onion = (member.member_server_onion or "").strip().lower()
                if not server_onion or is_local_server_authority(server_onion, self.settings):
                    continue
                remote_servers.add(server_onion)
            return remote_servers

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
                event_type="conversation.read",
                endpoint_path="/api/v2/federation/conversations/read",
                payload_json=payload,
                dedupe_key=f"conversation-read:{sender_key}:{peer_onion}",
            )
            await session.commit()
            await federation_outbox_service.deliver_item(session, outbox_item.id)

    async def _upsert_cursor(
        self,
        session: AsyncSession,
        *,
        conversation_id: str,
        user_id: str,
        user_address: str,
        last_read_message_id: str,
        last_read_sent_at_ms: int,
    ) -> tuple[ConversationReadCursor, bool]:
        stmt = select(ConversationReadCursor).where(
            ConversationReadCursor.conversation_id == conversation_id,
            ConversationReadCursor.user_id == user_id,
        )
        existing = (await session.execute(stmt)).scalar_one_or_none()
        if existing is None:
            created = ConversationReadCursor(
                conversation_id=conversation_id,
                user_id=user_id,
                user_address=user_address,
                last_read_message_id=last_read_message_id,
                last_read_sent_at_ms=last_read_sent_at_ms,
                created_at=datetime.now(UTC),
                updated_at=datetime.now(UTC),
            )
            session.add(created)
            await session.commit()
            await session.refresh(created)
            return created, True

        if last_read_sent_at_ms < existing.last_read_sent_at_ms:
            return existing, False
        if (
            last_read_sent_at_ms == existing.last_read_sent_at_ms
            and last_read_message_id == existing.last_read_message_id
        ):
            return existing, False

        existing.last_read_message_id = last_read_message_id
        existing.last_read_sent_at_ms = last_read_sent_at_ms
        existing.user_address = user_address
        existing.updated_at = datetime.now(UTC)
        await session.commit()
        await session.refresh(existing)
        return existing, True

    @staticmethod
    def _event_payload(cursor: ConversationReadCursor) -> dict:
        return {
            "type": "conversation.read",
            "conversation_id": cursor.conversation_id,
            "reader_user_address": cursor.user_address,
            "last_read_message_id": cursor.last_read_message_id,
            "last_read_sent_at_ms": cursor.last_read_sent_at_ms,
            "updated_at": cursor.updated_at.isoformat(),
        }

    async def publish_local_read_cursor(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        reader_user: User,
        last_read_message_id: str,
        last_read_sent_at_ms: int,
    ) -> ConversationReadCursorOutV2:
        self._ensure_enabled()
        await self._ensure_read_access(session, conversation, reader_user.id)

        message = (
            await session.execute(select(MessageEvent).where(MessageEvent.id == last_read_message_id))
        ).scalar_one_or_none()
        if message is None:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Message not found")
        if message.conversation_id != conversation.id:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Message does not belong to conversation")
        if int(message.sent_at_ms) != int(last_read_sent_at_ms):
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="last_read_sent_at_ms does not match message",
            )

        reader_address = server_address_for_username(reader_user.username)
        cursor, changed = await self._upsert_cursor(
            session,
            conversation_id=conversation.id,
            user_id=reader_user.id,
            user_address=reader_address,
            last_read_message_id=last_read_message_id,
            last_read_sent_at_ms=last_read_sent_at_ms,
        )
        if changed:
            event_payload = self._event_payload(cursor)
            local_recipients = await self._resolve_local_recipients(session, conversation)
            for user_id in local_recipients:
                await connection_manager.send_to_user(user_id, event_payload)

            remote_servers = await self._resolve_remote_peer_servers(session, conversation)
            relay_payload = {
                "relay_id": str(uuid4()),
                "conversation_id": conversation.id,
                "reader_user_address": reader_address,
                "last_read_message_id": cursor.last_read_message_id,
                "last_read_sent_at_ms": cursor.last_read_sent_at_ms,
                "updated_at": cursor.updated_at.isoformat(),
            }
            await self._fanout_remote(
                session,
                remote_servers=remote_servers,
                payload=relay_payload,
                sender_key=f"{conversation.id}:{reader_user.id}:{cursor.last_read_sent_at_ms}",
            )

        return ConversationReadCursorOutV2(
            conversation_id=cursor.conversation_id,
            reader_user_address=cursor.user_address,
            last_read_message_id=cursor.last_read_message_id,
            last_read_sent_at_ms=cursor.last_read_sent_at_ms,
            updated_at=cursor.updated_at,
        )

    async def get_read_state(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        reader_user: User,
    ) -> ConversationReadStateOutV2:
        self._ensure_enabled()
        await self._ensure_read_access(session, conversation, reader_user.id)
        rows = list(
            (
                await session.execute(
                    select(ConversationReadCursor)
                    .where(ConversationReadCursor.conversation_id == conversation.id)
                    .order_by(ConversationReadCursor.updated_at.desc())
                )
            ).scalars()
        )
        return ConversationReadStateOutV2(
            conversation_id=conversation.id,
            cursors=[
                ConversationReadCursorEntryOutV2(
                    user_address=row.user_address,
                    last_read_message_id=row.last_read_message_id,
                    last_read_sent_at_ms=row.last_read_sent_at_ms,
                    updated_at=row.updated_at,
                )
                for row in rows
            ],
        )

    async def relay_read_from_federation(
        self,
        session: AsyncSession,
        payload: FederationConversationReadRelayRequestV2,
    ) -> None:
        self._ensure_enabled()
        conversation = await conversation_service.get_by_id(session, payload.conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")

        reader_address = payload.reader_user_address.strip().lower()
        recipients: set[str] = set()
        if conversation.conversation_type == "group":
            members = await self._active_group_members(session, conversation.id)
            member_addresses = {row.member_address for row in members}
            if reader_address not in member_addresses:
                raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Read sender is not a group member")
            recipients = {row.member_user_id for row in members if row.member_user_id is not None}
        elif conversation.kind == "remote":
            if reader_address != (conversation.peer_address or "").strip().lower():
                raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Read sender mismatch")
            if conversation.local_user_id:
                recipients.add(conversation.local_user_id)

        event_payload = {
            "type": "conversation.read",
            "conversation_id": conversation.id,
            "reader_user_address": reader_address,
            "last_read_message_id": payload.last_read_message_id,
            "last_read_sent_at_ms": payload.last_read_sent_at_ms,
            "updated_at": payload.updated_at,
        }
        for user_id in recipients:
            await connection_manager.send_to_user(user_id, event_payload)


read_state_service_v2 = ReadStateServiceV2()
