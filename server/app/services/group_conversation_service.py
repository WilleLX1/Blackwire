from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime
from uuid import uuid4

from fastapi import HTTPException, status
from sqlalchemy import delete, func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.conversation_member import ConversationMember
from app.models.group_membership_event import GroupMembershipEvent
from app.models.user import User
from app.schemas.v2_conversation import ConversationMemberOutV2, ConversationOutV2, ConversationRecipientsOutV2
from app.schemas.v2_device import DeviceOutV2
from app.schemas.v2_federation import FederationGroupEventRequestV2, FederationGroupSnapshotOutV2
from app.services.device_service_v2 import device_service_v2
from app.services.federation_client import FederationClientError, federation_client
from app.services.federation_outbox_service import federation_outbox_service
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_authority import is_local_server_authority
from app.services.server_identity import get_server_onion, server_address_for_username
from app.ws.manager import connection_manager


@dataclass(slots=True)
class GroupEventRecord:
    group_uid: str
    event_seq: int
    event_type: str
    actor_address: str
    target_address: str
    payload: dict
    created_at: datetime


class GroupConversationService:
    def __init__(self) -> None:
        self.settings = get_settings()

    async def _notify_group_renamed_local(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        actor_address: str,
        event_seq: int = 0,
    ) -> None:
        members = await self._members_for_conversation(session, conversation.id)
        local_user_ids: set[str] = set()
        for row in members:
            if row.member_user_id is None:
                continue
            if row.status not in {"active", "invited"}:
                continue
            local_user_ids.add(row.member_user_id)
        if not local_user_ids:
            return

        payload = {
            "type": "conversation.group.renamed",
            "conversation_id": conversation.id,
            "group_uid": conversation.group_uid,
            "group_name": conversation.group_name,
            "actor_address": actor_address,
            "event_seq": int(event_seq or 0),
        }
        for user_id in local_user_ids:
            await connection_manager.send_to_user(user_id, payload)

    @staticmethod
    def _member_identity_key(member_user_id: str | None, member_address: str) -> str:
        if member_user_id:
            return f"user:{member_user_id}"
        return f"addr:{member_address}"

    async def _ensure_owner_member_row(self, session: AsyncSession, conversation: Conversation) -> None:
        if conversation.conversation_type != "group":
            return
        owner_address = (conversation.owner_address or "").strip().lower()
        if not owner_address:
            return
        stmt = select(ConversationMember).where(
            ConversationMember.conversation_id == conversation.id,
            ConversationMember.member_address == owner_address,
        )
        existing = (await session.execute(stmt)).scalar_one_or_none()
        if existing is not None:
            if existing.member_user_id is None or existing.role != "owner" or existing.status != "active":
                try:
                    member_user_id, member_server_onion = await self._resolve_member_user_id(
                        session,
                        owner_address,
                    )
                    existing.member_user_id = member_user_id
                    if member_server_onion:
                        existing.member_server_onion = member_server_onion
                except HTTPException:
                    pass
                existing.role = "owner"
                existing.status = "active"
                if existing.joined_at is None:
                    existing.joined_at = datetime.now(UTC)
                existing.left_at = None
                existing.updated_at = datetime.now(UTC)
                await session.flush()
            return

        now = datetime.now(UTC)
        member_user_id: str | None = None
        member_server_onion = ""
        try:
            member_user_id, member_server_onion = await self._resolve_member_user_id(
                session,
                owner_address,
            )
        except HTTPException:
            try:
                parsed = parse_peer_address_with_policy(owner_address, self.settings.tor_enabled)
                member_server_onion = parsed.server_onion
            except ValueError:
                member_server_onion = ""

        session.add(
            ConversationMember(
                conversation_id=conversation.id,
                member_user_id=member_user_id,
                member_address=owner_address,
                member_server_onion=member_server_onion,
                role="owner",
                status="active",
                invited_by_address=owner_address,
                invited_at=now,
                joined_at=now,
                updated_at=now,
            )
        )
        await session.flush()

    async def _next_event_seq(self, session: AsyncSession, group_uid: str) -> int:
        stmt = select(func.max(GroupMembershipEvent.event_seq)).where(GroupMembershipEvent.group_uid == group_uid)
        current = (await session.execute(stmt)).scalar_one_or_none()
        return 1 if current is None else int(current) + 1

    async def _current_event_seq(self, session: AsyncSession, group_uid: str) -> int:
        stmt = select(func.max(GroupMembershipEvent.event_seq)).where(GroupMembershipEvent.group_uid == group_uid)
        current = (await session.execute(stmt)).scalar_one_or_none()
        return int(current or 0)

    async def _event_exists(self, session: AsyncSession, group_uid: str, event_seq: int) -> bool:
        stmt = (
            select(GroupMembershipEvent.id)
            .where(
                GroupMembershipEvent.group_uid == group_uid,
                GroupMembershipEvent.event_seq == event_seq,
            )
            .limit(1)
        )
        return (await session.execute(stmt)).scalar_one_or_none() is not None

    async def _append_event(
        self,
        session: AsyncSession,
        *,
        group_uid: str,
        event_seq: int,
        event_type: str,
        actor_address: str,
        target_address: str,
        payload: dict,
        created_at: datetime | None = None,
    ) -> GroupMembershipEvent:
        row = GroupMembershipEvent(
            group_uid=group_uid,
            event_seq=event_seq,
            event_type=event_type,
            actor_address=actor_address,
            target_address=target_address,
            payload_json=payload,
            created_at=created_at or datetime.now(UTC),
        )
        session.add(row)
        await session.flush()
        return row

    async def _serialize_event(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        event_type: str,
        actor_address: str,
        target_address: str,
        payload: dict,
        created_at: datetime | None = None,
    ) -> GroupEventRecord:
        if not conversation.group_uid:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Group conversation missing group_uid")
        seq = await self._next_event_seq(session, conversation.group_uid)
        row = await self._append_event(
            session,
            group_uid=conversation.group_uid,
            event_seq=seq,
            event_type=event_type,
            actor_address=actor_address,
            target_address=target_address,
            payload=payload,
            created_at=created_at,
        )
        return GroupEventRecord(
            group_uid=row.group_uid,
            event_seq=row.event_seq,
            event_type=row.event_type,
            actor_address=row.actor_address,
            target_address=row.target_address,
            payload=row.payload_json,
            created_at=row.created_at,
        )

    async def _members_for_conversation(self, session: AsyncSession, conversation_id: str) -> list[ConversationMember]:
        stmt = (
            select(ConversationMember)
            .where(ConversationMember.conversation_id == conversation_id)
            .order_by(ConversationMember.invited_at.asc())
        )
        rows = list((await session.execute(stmt)).scalars().all())
        by_address: dict[str, ConversationMember] = {}
        for row in rows:
            existing = by_address.get(row.member_address)
            if existing is None or row.updated_at >= existing.updated_at:
                by_address[row.member_address] = row
        return list(by_address.values())

    async def _member_for_user(
        self,
        session: AsyncSession,
        conversation_id: str,
        user_id: str,
    ) -> ConversationMember | None:
        stmt = (
            select(ConversationMember)
            .where(
                ConversationMember.conversation_id == conversation_id,
                ConversationMember.member_user_id == user_id,
            )
            .order_by(ConversationMember.role.desc(), ConversationMember.status.asc(), ConversationMember.updated_at.desc())
        )
        return (await session.execute(stmt)).scalars().first()

    async def ensure_member_access(
        self,
        session: AsyncSession,
        conversation: Conversation,
        user_id: str,
        *,
        require_active: bool = True,
    ) -> ConversationMember:
        await self._ensure_owner_member_row(session, conversation)
        member = await self._member_for_user(session, conversation.id, user_id)
        if member is None:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a conversation member")
        if require_active and member.status != "active":
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not an active conversation member")
        return member

    async def ensure_owner(self, session: AsyncSession, conversation: Conversation, user_id: str) -> ConversationMember:
        member = await self.ensure_member_access(session, conversation, user_id, require_active=True)
        if member.role != "owner":
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Only owner can perform this action")
        return member

    def _normalize_name(self, value: str) -> str:
        normalized = value.strip()
        if not normalized:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Group name is required")
        return normalized

    def _normalize_member_address(self, value: str) -> str:
        try:
            parsed = parse_peer_address_with_policy(value, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
        return parsed.canonical

    async def _resolve_member_user_id(
        self,
        session: AsyncSession,
        member_address: str,
        request_authority: str | None = None,
    ) -> tuple[str | None, str]:
        parsed = parse_peer_address_with_policy(member_address, self.settings.tor_enabled)
        additional_aliases = {request_authority} if request_authority else None
        if is_local_server_authority(parsed.server_onion, self.settings, additional_aliases):
            user_stmt = select(User).where(User.username == parsed.username, User.disabled_at.is_(None))
            user = (await session.execute(user_stmt)).scalar_one_or_none()
            if user is None:
                raise HTTPException(
                    status_code=status.HTTP_404_NOT_FOUND,
                    detail=f"Local user not found for {parsed.canonical}",
                )
            return user.id, parsed.server_onion
        return None, parsed.server_onion

    async def _broadcast_events(
        self,
        session: AsyncSession,
        conversation: Conversation,
        events: list[GroupEventRecord],
    ) -> None:
        if conversation.origin_server_onion != get_server_onion():
            return
        members = await self._members_for_conversation(session, conversation.id)
        remote_servers = {
            member.member_server_onion
            for member in members
            if member.member_server_onion
            and not is_local_server_authority(member.member_server_onion, self.settings)
            and member.status in {"invited", "active"}
        }
        for peer_onion in remote_servers:
            for event in events:
                payload = {
                    "relay_id": str(uuid4()),
                    "group_uid": event.group_uid,
                    "event_seq": event.event_seq,
                    "event_type": event.event_type,
                    "actor_address": event.actor_address,
                    "target_address": event.target_address,
                    "payload": event.payload,
                    "created_at": event.created_at.isoformat(),
                }
                outbox_item = await federation_outbox_service.enqueue(
                    session,
                    peer_onion=peer_onion,
                    event_type="group.event",
                    endpoint_path="/api/v2/federation/groups/events",
                    payload_json=payload,
                    dedupe_key=f"group-event:{event.group_uid}:{event.event_seq}:{peer_onion}",
                )
                await federation_outbox_service.deliver_item(session, outbox_item.id)

    async def create_group(
        self,
        session: AsyncSession,
        owner: User,
        *,
        name: str,
        member_addresses: list[str],
        request_authority: str | None = None,
    ) -> Conversation:
        if not self.settings.enable_group_dm_v2c:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group conversations are disabled")
        owner_address = server_address_for_username(owner.username)
        normalized_name = self._normalize_name(name)
        seen: set[str] = {owner_address}
        seen_identity_keys: set[str] = {self._member_identity_key(owner.id, owner_address)}
        normalized_members: list[str] = []
        normalized_member_identity_keys: list[str] = []
        for raw in member_addresses:
            canonical = self._normalize_member_address(raw)
            if canonical in seen:
                continue
            member_user_id, _ = await self._resolve_member_user_id(
                session,
                canonical,
                request_authority=request_authority,
            )
            if member_user_id == owner.id:
                continue
            member_identity_key = self._member_identity_key(member_user_id, canonical)
            if member_identity_key in seen_identity_keys:
                continue
            seen.add(canonical)
            seen_identity_keys.add(member_identity_key)
            normalized_members.append(canonical)
            normalized_member_identity_keys.append(member_identity_key)
        if 1 + len(normalized_member_identity_keys) > self.settings.group_max_members:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail=(
                    f"Group member limit exceeded "
                    f"(requested_total={1 + len(normalized_member_identity_keys)}, "
                    f"max={self.settings.group_max_members})"
                ),
            )

        now = datetime.now(UTC)
        conversation = Conversation(
            kind="group",
            conversation_type="group",
            group_uid=uuid4().hex,
            group_name=normalized_name,
            origin_server_onion=get_server_onion(),
            owner_address=owner_address,
            created_at=now,
        )
        session.add(conversation)
        await session.flush()
        session.add(
            ConversationMember(
                conversation_id=conversation.id,
                member_user_id=owner.id,
                member_address=owner_address,
                member_server_onion=get_server_onion(),
                role="owner",
                status="active",
                invited_by_address=owner_address,
                invited_at=now,
                joined_at=now,
                updated_at=now,
            )
        )
        invite_rows: list[ConversationMember] = []
        for address in normalized_members:
            member_user_id, member_server = await self._resolve_member_user_id(
                session,
                address,
                request_authority=request_authority,
            )
            if member_user_id == owner.id:
                continue
            row = ConversationMember(
                conversation_id=conversation.id,
                member_user_id=member_user_id,
                member_address=address,
                member_server_onion=member_server,
                role="member",
                status="invited",
                invited_by_address=owner_address,
                invited_at=now,
                updated_at=now,
            )
            session.add(row)
            invite_rows.append(row)

        events = [
            await self._serialize_event(
                session,
                conversation=conversation,
                event_type="create",
                actor_address=owner_address,
                target_address="",
                payload={
                    "conversation_id": conversation.id,
                    "group_uid": conversation.group_uid,
                    "group_name": conversation.group_name,
                    "origin_server_onion": conversation.origin_server_onion,
                    "owner_address": conversation.owner_address,
                },
                created_at=now,
            )
        ]
        for row in invite_rows:
            events.append(
                await self._serialize_event(
                    session,
                    conversation=conversation,
                    event_type="invite",
                    actor_address=owner_address,
                    target_address=row.member_address,
                    payload={
                        "conversation_id": conversation.id,
                        "group_uid": conversation.group_uid,
                        "member_address": row.member_address,
                        "member_server_onion": row.member_server_onion,
                        "status": "invited",
                        "role": row.role,
                    },
                    created_at=now,
                )
            )
        await self._broadcast_events(session, conversation, events)
        await session.commit()
        await metrics.inc("groups.created")
        return conversation

    async def list_members(
        self,
        session: AsyncSession,
        conversation: Conversation,
        user_id: str,
    ) -> list[ConversationMember]:
        await self.ensure_member_access(session, conversation, user_id, require_active=False)
        return await self._members_for_conversation(session, conversation.id)

    async def invite_members(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        owner: User,
        member_addresses: list[str],
        request_authority: str | None = None,
    ) -> list[ConversationMember]:
        await self.ensure_owner(session, conversation, owner.id)
        if conversation.origin_server_onion != get_server_onion():
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Only origin server can modify membership")
        current = await self._members_for_conversation(session, conversation.id)
        by_address = {row.member_address: row for row in current}
        active_identity_keys: set[str] = set()
        for row in current:
            if row.status not in {"active", "invited"}:
                continue
            active_identity_keys.add(self._member_identity_key(row.member_user_id, row.member_address))
        active_count = len(active_identity_keys)
        owner_address = server_address_for_username(owner.username)
        now = datetime.now(UTC)
        rows: list[ConversationMember] = []
        events: list[GroupEventRecord] = []
        for raw in member_addresses:
            canonical = self._normalize_member_address(raw)
            if canonical == owner_address:
                continue
            existing = by_address.get(canonical)
            if existing is not None and existing.member_user_id == owner.id:
                continue
            if existing is not None and existing.status in {"active", "invited"}:
                continue
            if existing is None:
                member_user_id, member_server = await self._resolve_member_user_id(
                    session,
                    canonical,
                    request_authority=request_authority,
                )
                if member_user_id == owner.id:
                    continue
                member_identity_key = self._member_identity_key(member_user_id, canonical)
                if member_identity_key in active_identity_keys:
                    continue
                if active_count + 1 > self.settings.group_max_members:
                    raise HTTPException(
                        status_code=status.HTTP_400_BAD_REQUEST,
                        detail=(
                            "Group member limit exceeded "
                            f"(active_or_invited={active_count}, max={self.settings.group_max_members})"
                        ),
                    )
                active_identity_keys.add(member_identity_key)
                active_count += 1
                existing = ConversationMember(
                    conversation_id=conversation.id,
                    member_user_id=member_user_id,
                    member_address=canonical,
                    member_server_onion=member_server,
                    role="member",
                    status="invited",
                    invited_by_address=owner_address,
                    invited_at=now,
                    updated_at=now,
                )
                session.add(existing)
            else:
                member_identity_key = self._member_identity_key(existing.member_user_id, existing.member_address)
                if member_identity_key not in active_identity_keys:
                    if active_count + 1 > self.settings.group_max_members:
                        raise HTTPException(
                            status_code=status.HTTP_400_BAD_REQUEST,
                            detail=(
                                "Group member limit exceeded "
                                f"(active_or_invited={active_count}, max={self.settings.group_max_members})"
                            ),
                        )
                    active_identity_keys.add(member_identity_key)
                    active_count += 1
                existing.status = "invited"
                existing.role = "member"
                existing.invited_by_address = owner_address
                existing.invited_at = now
                existing.joined_at = None
                existing.left_at = None
                existing.updated_at = now
            rows.append(existing)
            events.append(
                await self._serialize_event(
                    session,
                    conversation=conversation,
                    event_type="invite",
                    actor_address=owner_address,
                    target_address=canonical,
                    payload={
                        "conversation_id": conversation.id,
                        "group_uid": conversation.group_uid,
                        "member_address": canonical,
                        "member_server_onion": existing.member_server_onion,
                        "status": "invited",
                        "role": "member",
                    },
                    created_at=now,
                )
            )
        if events:
            await self._broadcast_events(session, conversation, events)
            await session.commit()
            await metrics.inc("groups.invited", len(events))
        return rows

    async def remove_member(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        owner: User,
        member_address: str,
    ) -> ConversationMember:
        await self.ensure_owner(session, conversation, owner.id)
        target = self._normalize_member_address(member_address)
        owner_address = server_address_for_username(owner.username)
        if target == owner_address:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Owner cannot remove self")
        stmt = select(ConversationMember).where(
            ConversationMember.conversation_id == conversation.id,
            ConversationMember.member_address == target,
        )
        row = (await session.execute(stmt)).scalar_one_or_none()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Member not found")
        now = datetime.now(UTC)
        row.status = "removed"
        row.left_at = now
        row.updated_at = now
        event = await self._serialize_event(
            session,
            conversation=conversation,
            event_type="remove",
            actor_address=owner_address,
            target_address=target,
            payload={"conversation_id": conversation.id, "group_uid": conversation.group_uid, "member_address": target},
            created_at=now,
        )
        await self._broadcast_events(session, conversation, [event])
        await session.commit()
        await metrics.inc("groups.removed")
        return row

    async def leave(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        user: User,
        reason: str | None = None,
    ) -> ConversationMember:
        member = await self.ensure_member_access(session, conversation, user.id, require_active=False)
        now = datetime.now(UTC)
        actor_address = (member.member_address or server_address_for_username(user.username)).strip().lower()
        leave_reason = (reason or "").strip() or "left"

        if member.role == "owner" and member.status in {"active", "invited"}:
            eligible_stmt = (
                select(ConversationMember)
                .where(
                    ConversationMember.conversation_id == conversation.id,
                    ConversationMember.role != "owner",
                    ConversationMember.status.in_(("active", "invited")),
                )
                .order_by(ConversationMember.invited_at.asc(), ConversationMember.member_address.asc())
                .limit(1)
            )
            replacement = (await session.execute(eligible_stmt)).scalar_one_or_none()

            if replacement is not None:
                member.role = "member"
                member.status = "left"
                member.left_at = now
                member.updated_at = now

                replacement.role = "owner"
                replacement.status = "active"
                if replacement.joined_at is None:
                    replacement.joined_at = now
                replacement.left_at = None
                replacement.updated_at = now
                conversation.owner_address = replacement.member_address

                if conversation.origin_server_onion == get_server_onion():
                    transfer_event = await self._serialize_event(
                        session,
                        conversation=conversation,
                        event_type="transfer_owner",
                        actor_address=actor_address,
                        target_address=replacement.member_address,
                        payload={
                            "conversation_id": conversation.id,
                            "group_uid": conversation.group_uid,
                            "previous_owner_address": actor_address,
                            "owner_address": replacement.member_address,
                            "member_address": replacement.member_address,
                        },
                        created_at=now,
                    )
                    leave_event = await self._serialize_event(
                        session,
                        conversation=conversation,
                        event_type="leave",
                        actor_address=actor_address,
                        target_address=actor_address,
                        payload={
                            "conversation_id": conversation.id,
                            "group_uid": conversation.group_uid,
                            "member_address": actor_address,
                            "reason": leave_reason,
                        },
                        created_at=now,
                    )
                    await self._broadcast_events(session, conversation, [transfer_event, leave_event])

                await session.commit()
                await metrics.inc("groups.owner_transferred")
                await metrics.inc("groups.left")
                return member

            synthetic = ConversationMember(
                id=member.id,
                conversation_id=member.conversation_id,
                member_user_id=member.member_user_id,
                member_address=member.member_address,
                member_server_onion=member.member_server_onion,
                role="member",
                status="left",
                invited_by_address=member.invited_by_address,
                invited_at=member.invited_at,
                joined_at=member.joined_at,
                left_at=now,
                updated_at=now,
            )
            await session.delete(conversation)
            await session.commit()
            await metrics.inc("groups.deleted_on_owner_leave")
            await metrics.inc("groups.left")
            return synthetic

        member.status = "left"
        member.left_at = now
        member.updated_at = now
        if conversation.origin_server_onion == get_server_onion():
            event = await self._serialize_event(
                session,
                conversation=conversation,
                event_type="leave",
                actor_address=actor_address,
                target_address=actor_address,
                payload={
                    "conversation_id": conversation.id,
                    "group_uid": conversation.group_uid,
                    "member_address": actor_address,
                    "reason": leave_reason,
                },
                created_at=now,
            )
            await self._broadcast_events(session, conversation, [event])
        await session.commit()
        await metrics.inc("groups.left")
        return member

    async def rename(self, session: AsyncSession, *, conversation: Conversation, owner: User, name: str) -> Conversation:
        await self.ensure_owner(session, conversation, owner.id)
        conversation.group_name = self._normalize_name(name)
        now = datetime.now(UTC)
        actor_address = server_address_for_username(owner.username)
        event = await self._serialize_event(
            session,
            conversation=conversation,
            event_type="rename",
            actor_address=actor_address,
            target_address="",
            payload={"conversation_id": conversation.id, "group_uid": conversation.group_uid, "group_name": conversation.group_name},
            created_at=now,
        )
        await self._broadcast_events(session, conversation, [event])
        await session.commit()
        await self._notify_group_renamed_local(
            session,
            conversation=conversation,
            actor_address=actor_address,
            event_seq=event.event_seq,
        )
        await metrics.inc("groups.rename")
        return conversation

    async def accept_invite(self, session: AsyncSession, *, conversation: Conversation, user: User) -> ConversationMember:
        member = await self.ensure_member_access(session, conversation, user.id, require_active=False)
        actor_address = server_address_for_username(user.username)
        now = datetime.now(UTC)
        member.status = "active"
        member.joined_at = now
        member.left_at = None
        member.updated_at = now
        if conversation.origin_server_onion == get_server_onion():
            event = await self._serialize_event(
                session,
                conversation=conversation,
                event_type="accept",
                actor_address=actor_address,
                target_address=actor_address,
                payload={"conversation_id": conversation.id, "group_uid": conversation.group_uid, "member_address": actor_address},
                created_at=now,
            )
            await self._broadcast_events(session, conversation, [event])
        else:
            try:
                await federation_client.post_signed(
                    conversation.origin_server_onion,
                    "/api/v2/federation/groups/invites/accept",
                    {
                        "relay_id": str(uuid4()),
                        "group_uid": conversation.group_uid,
                        "conversation_id": conversation.id,
                        "actor_address": actor_address,
                    },
                )
            except FederationClientError as exc:
                raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=exc.detail) from exc
        await session.commit()
        await metrics.inc("groups.accepted")
        return member

    async def members_for_recipients(
        self,
        session: AsyncSession,
        *,
        conversation: Conversation,
        user: User,
        current_device_uid: str | None = None,
    ) -> ConversationRecipientsOutV2:
        await self.ensure_member_access(session, conversation, user.id, require_active=True)
        members = await self._members_for_conversation(session, conversation.id)
        recipients = []
        for member in members:
            if member.status != "active":
                continue
            lookup = await device_service_v2.resolve_devices_by_peer_address(session, member.member_address)
            if lookup is None:
                continue
            for device in lookup.devices:
                if member.member_user_id == user.id and current_device_uid and device.device_uid == current_device_uid:
                    continue
                recipients.append(
                    {
                        "member_address": member.member_address,
                        "member_status": "active",
                        "device": DeviceOutV2.model_validate(device),
                        "prekey": None,
                    }
                )
        return ConversationRecipientsOutV2(conversation_id=conversation.id, conversation_type="group", recipients=recipients)

    async def to_conversation_out(self, session: AsyncSession, *, conversation: Conversation, user: User) -> ConversationOutV2:
        members = await self._members_for_conversation(session, conversation.id)
        member = next((row for row in members if row.member_user_id == user.id), None)
        member_count_keys: set[str] = set()
        for row in members:
            if row.status not in {"active", "invited"}:
                continue
            member_count_keys.add(self._member_identity_key(row.member_user_id, row.member_address))
        return ConversationOutV2(
            id=conversation.id,
            kind=conversation.kind,
            user_a_id=conversation.user_a_id or "",
            user_b_id=conversation.user_b_id or "",
            local_user_id=conversation.local_user_id or "",
            created_at=conversation.created_at,
            peer_username=conversation.peer_username,
            peer_server_onion=conversation.peer_server_onion,
            peer_address=conversation.peer_address,
            conversation_type="group",
            group_uid=conversation.group_uid,
            group_name=conversation.group_name,
            member_count=len(member_count_keys),
            membership_state=("none" if member is None else member.status),  # type: ignore[arg-type]
            can_manage_members=bool(member is not None and member.role == "owner" and member.status == "active"),
            origin_server_onion=conversation.origin_server_onion,
            owner_address=conversation.owner_address,
        )

    async def get_group_by_uid(self, session: AsyncSession, group_uid: str) -> Conversation | None:
        stmt = select(Conversation).where(Conversation.group_uid == group_uid, Conversation.conversation_type == "group")
        return (await session.execute(stmt)).scalar_one_or_none()

    async def _apply_snapshot(self, session: AsyncSession, snapshot: FederationGroupSnapshotOutV2) -> Conversation:
        conversation = await self.get_group_by_uid(session, snapshot.group_uid)
        if conversation is None:
            conversation = Conversation(
                kind="group_remote",
                conversation_type="group",
                group_uid=snapshot.group_uid,
                group_name=snapshot.group_name,
                origin_server_onion=snapshot.origin_server_onion,
                owner_address=snapshot.owner_address,
            )
            session.add(conversation)
            await session.flush()
        else:
            conversation.group_name = snapshot.group_name
            conversation.origin_server_onion = snapshot.origin_server_onion
            conversation.owner_address = snapshot.owner_address

        await session.execute(delete(ConversationMember).where(ConversationMember.conversation_id == conversation.id))
        for member_payload in snapshot.members:
            member_address = str(member_payload.get("member_address", "")).strip().lower()
            if not member_address:
                continue
            parsed = parse_peer_address_with_policy(member_address, self.settings.tor_enabled)
            member_user_id = member_payload.get("member_user_id")
            if is_local_server_authority(parsed.server_onion, self.settings):
                user_stmt = select(User).where(User.username == parsed.username, User.disabled_at.is_(None))
                local_user = (await session.execute(user_stmt)).scalar_one_or_none()
                member_user_id = local_user.id if local_user is not None else None
            invited_at_raw = str(member_payload.get("invited_at", datetime.now(UTC).isoformat()))
            invited_at = datetime.fromisoformat(invited_at_raw.replace("Z", "+00:00"))
            joined_at_raw = member_payload.get("joined_at")
            left_at_raw = member_payload.get("left_at")
            updated_at_raw = str(member_payload.get("updated_at", invited_at_raw))
            session.add(
                ConversationMember(
                    conversation_id=conversation.id,
                    member_user_id=member_user_id,
                    member_address=parsed.canonical,
                    member_server_onion=parsed.server_onion,
                    role=str(member_payload.get("role", "member")),
                    status=str(member_payload.get("status", "invited")),
                    invited_by_address=str(member_payload.get("invited_by_address", "")),
                    invited_at=invited_at,
                    joined_at=None if not joined_at_raw else datetime.fromisoformat(str(joined_at_raw).replace("Z", "+00:00")),
                    left_at=None if not left_at_raw else datetime.fromisoformat(str(left_at_raw).replace("Z", "+00:00")),
                    updated_at=datetime.fromisoformat(updated_at_raw.replace("Z", "+00:00")),
                )
            )
        await session.execute(delete(GroupMembershipEvent).where(GroupMembershipEvent.group_uid == snapshot.group_uid))
        if snapshot.latest_event_seq > 0:
            session.add(
                GroupMembershipEvent(
                    group_uid=snapshot.group_uid,
                    event_seq=snapshot.latest_event_seq,
                    event_type="snapshot",
                    actor_address=snapshot.owner_address,
                    target_address="",
                    payload_json={"source": "federation_snapshot"},
                    created_at=datetime.now(UTC),
                )
            )
        await session.flush()
        return conversation

    async def snapshot_for_group(self, session: AsyncSession, group_uid: str) -> FederationGroupSnapshotOutV2:
        conversation = await self.get_group_by_uid(session, group_uid)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group not found")
        members = await self._members_for_conversation(session, conversation.id)
        latest_stmt = select(func.max(GroupMembershipEvent.event_seq)).where(GroupMembershipEvent.group_uid == group_uid)
        latest = int((await session.execute(latest_stmt)).scalar_one_or_none() or 0)
        payload_members = [
            {
                "member_user_id": row.member_user_id,
                "member_address": row.member_address,
                "member_server_onion": row.member_server_onion,
                "role": row.role,
                "status": row.status,
                "invited_by_address": row.invited_by_address,
                "invited_at": row.invited_at.isoformat(),
                "joined_at": row.joined_at.isoformat() if row.joined_at else None,
                "left_at": row.left_at.isoformat() if row.left_at else None,
                "updated_at": row.updated_at.isoformat(),
            }
            for row in members
        ]
        return FederationGroupSnapshotOutV2(
            group_uid=group_uid,
            conversation_id=conversation.id,
            group_name=conversation.group_name,
            origin_server_onion=conversation.origin_server_onion,
            owner_address=conversation.owner_address,
            latest_event_seq=latest,
            members=payload_members,
        )

    async def apply_federation_event(
        self,
        session: AsyncSession,
        *,
        payload: FederationGroupEventRequestV2,
        sender_onion: str | None = None,
    ) -> None:
        current_seq = await self._current_event_seq(session, payload.group_uid)
        if payload.event_seq <= current_seq:
            if await self._event_exists(session, payload.group_uid, payload.event_seq):
                return
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Group event sequence conflict")
        expected_seq = current_seq + 1
        if payload.event_seq > expected_seq:
            if not sender_onion:
                raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Group event sequence gap")
            try:
                snapshot = await federation_client.get_remote_group_snapshot_v2(sender_onion, payload.group_uid)
            except FederationClientError as exc:
                raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=exc.detail) from exc
            await self._apply_snapshot(session, snapshot)
            current_seq = await self._current_event_seq(session, payload.group_uid)
            expected_seq = current_seq + 1
            if payload.event_seq <= current_seq:
                if await self._event_exists(session, payload.group_uid, payload.event_seq):
                    return
                raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Group event sequence conflict")
            if payload.event_seq > expected_seq:
                raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Group event sequence gap")

        conversation = await self.get_group_by_uid(session, payload.group_uid)
        if conversation is None and payload.event_type != "create":
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group not found")
        if conversation is None:
            conversation = Conversation(
                kind="group_remote",
                conversation_type="group",
                group_uid=payload.group_uid,
                group_name=str(payload.payload.get("group_name", "")),
                origin_server_onion=str(payload.payload.get("origin_server_onion", "")),
                owner_address=str(payload.payload.get("owner_address", payload.actor_address)),
            )
            session.add(conversation)
            await session.flush()
        created_at = datetime.fromisoformat(payload.created_at.replace("Z", "+00:00"))
        if payload.event_type == "create":
            conversation.group_name = str(payload.payload.get("group_name", conversation.group_name))
            conversation.origin_server_onion = str(payload.payload.get("origin_server_onion", conversation.origin_server_onion))
            conversation.owner_address = str(payload.payload.get("owner_address", conversation.owner_address or payload.actor_address))
        if payload.event_type == "rename":
            conversation.group_name = str(payload.payload.get("group_name", conversation.group_name))
        elif payload.event_type == "transfer_owner":
            prior_owner_raw = (
                str(payload.payload.get("previous_owner_address", payload.actor_address or "")).strip().lower()
            )
            next_owner_raw = str(
                payload.payload.get("owner_address")
                or payload.target_address
                or payload.payload.get("member_address")
                or ""
            ).strip().lower()
            if next_owner_raw:
                parsed_next_owner = parse_peer_address_with_policy(next_owner_raw, self.settings.tor_enabled)
                conversation.owner_address = parsed_next_owner.canonical

                prior_owner_canonical = ""
                if prior_owner_raw:
                    try:
                        prior_owner_canonical = parse_peer_address_with_policy(
                            prior_owner_raw, self.settings.tor_enabled
                        ).canonical
                    except ValueError:
                        prior_owner_canonical = ""

                if prior_owner_canonical:
                    prior_owner_stmt = select(ConversationMember).where(
                        ConversationMember.conversation_id == conversation.id,
                        ConversationMember.member_address == prior_owner_canonical,
                    )
                    prior_owner_row = (await session.execute(prior_owner_stmt)).scalar_one_or_none()
                    if prior_owner_row is not None:
                        prior_owner_row.role = "member"
                        prior_owner_row.updated_at = created_at

                next_owner_stmt = select(ConversationMember).where(
                    ConversationMember.conversation_id == conversation.id,
                    ConversationMember.member_address == parsed_next_owner.canonical,
                )
                next_owner_row = (await session.execute(next_owner_stmt)).scalar_one_or_none()
                if next_owner_row is None:
                    next_owner_row = ConversationMember(
                        conversation_id=conversation.id,
                        member_user_id=None,
                        member_address=parsed_next_owner.canonical,
                        member_server_onion=parsed_next_owner.server_onion,
                        role="owner",
                        status="active",
                        invited_by_address=payload.actor_address,
                        invited_at=created_at,
                        joined_at=created_at,
                        updated_at=created_at,
                    )
                    session.add(next_owner_row)
                else:
                    next_owner_row.role = "owner"
                    next_owner_row.status = "active"
                    if next_owner_row.joined_at is None:
                        next_owner_row.joined_at = created_at
                    next_owner_row.left_at = None
                    next_owner_row.updated_at = created_at
        elif payload.event_type in {"invite", "accept", "remove", "leave", "create"}:
            target = payload.target_address or payload.actor_address or conversation.owner_address
            parsed = parse_peer_address_with_policy(target, self.settings.tor_enabled)
            member_stmt = select(ConversationMember).where(
                ConversationMember.conversation_id == conversation.id,
                ConversationMember.member_address == parsed.canonical,
            )
            row = (await session.execute(member_stmt)).scalar_one_or_none()
            if row is None:
                row = ConversationMember(
                    conversation_id=conversation.id,
                    member_user_id=None,
                    member_address=parsed.canonical,
                    member_server_onion=parsed.server_onion,
                    role="owner" if payload.event_type == "create" else "member",
                    status="active" if payload.event_type in {"create", "accept"} else "invited",
                    invited_by_address=payload.actor_address,
                    invited_at=created_at,
                    joined_at=created_at if payload.event_type in {"create", "accept"} else None,
                    updated_at=created_at,
                )
                session.add(row)
            else:
                row.status = (
                    "active"
                    if payload.event_type in {"accept", "create"}
                    else "removed"
                    if payload.event_type == "remove"
                    else "left"
                    if payload.event_type == "leave"
                    else "invited"
                )
                if payload.event_type == "create":
                    row.role = "owner"
                row.updated_at = created_at
                if row.status == "active":
                    row.joined_at = created_at
                    row.left_at = None
                if row.status in {"removed", "left"}:
                    row.left_at = created_at
        await self._append_event(
            session,
            group_uid=payload.group_uid,
            event_seq=payload.event_seq,
            event_type=payload.event_type,
            actor_address=payload.actor_address,
            target_address=payload.target_address,
            payload=payload.payload,
            created_at=created_at,
        )
        await session.commit()
        if payload.event_type == "rename":
            await self._notify_group_renamed_local(
                session,
                conversation=conversation,
                actor_address=payload.actor_address,
                event_seq=payload.event_seq,
            )

    async def accept_remote_invite_to_origin(self, session: AsyncSession, *, group_uid: str, actor_address: str) -> None:
        conversation = await self.get_group_by_uid(session, group_uid)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group not found")
        stmt = select(ConversationMember).where(
            ConversationMember.conversation_id == conversation.id,
            ConversationMember.member_address == actor_address,
        )
        row = (await session.execute(stmt)).scalar_one_or_none()
        if row is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Member not found")
        now = datetime.now(UTC)
        row.status = "active"
        row.joined_at = now
        row.left_at = None
        row.updated_at = now
        event = await self._serialize_event(
            session,
            conversation=conversation,
            event_type="accept",
            actor_address=actor_address,
            target_address=actor_address,
            payload={"conversation_id": conversation.id, "group_uid": conversation.group_uid, "member_address": actor_address},
            created_at=now,
        )
        await self._broadcast_events(session, conversation, [event])
        await session.commit()

    def member_out(self, row: ConversationMember) -> ConversationMemberOutV2:
        return ConversationMemberOutV2(
            id=row.id,
            member_user_id=row.member_user_id,
            member_address=row.member_address,
            member_server_onion=row.member_server_onion,
            role=row.role,  # type: ignore[arg-type]
            status=row.status,  # type: ignore[arg-type]
            invited_by_address=row.invited_by_address,
            invited_at=row.invited_at,
            joined_at=row.joined_at,
            left_at=row.left_at,
            updated_at=row.updated_at,
        )


group_conversation_service = GroupConversationService()
