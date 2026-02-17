from fastapi import HTTPException, status
from sqlalchemy import and_, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.user import User
from app.services.peer_address import parse_peer_address, parse_peer_address_with_policy
from app.services.server_identity import get_server_onion, server_address_for_username


class ConversationService:
    def __init__(self) -> None:
        self.settings = get_settings()

    @staticmethod
    def _sorted_pair(left: str, right: str) -> tuple[str, str]:
        if left < right:
            return left, right
        return right, left

    async def get_by_id(self, session: AsyncSession, conversation_id: str) -> Conversation | None:
        stmt = select(Conversation).where(Conversation.id == conversation_id)
        return (await session.execute(stmt)).scalar_one_or_none()

    async def _create_local_dm(self, session: AsyncSession, user: User, peer_username: str) -> Conversation:
        normalized = peer_username.strip().lower()
        peer_stmt = select(User).where(User.username == normalized, User.disabled_at.is_(None))
        peer = (await session.execute(peer_stmt)).scalar_one_or_none()
        if peer is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Peer user not found")
        if peer.id == user.id:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Cannot open self DM")

        user_a, user_b = self._sorted_pair(user.id, peer.id)
        existing_stmt = select(Conversation).where(
            Conversation.kind == "local",
            Conversation.user_a_id == user_a,
            Conversation.user_b_id == user_b,
        )
        existing = (await session.execute(existing_stmt)).scalar_one_or_none()
        if existing is not None:
            return existing

        conversation = Conversation(
            kind="local",
            user_a_id=user_a,
            user_b_id=user_b,
            peer_server_onion=get_server_onion(),
        )
        session.add(conversation)
        await session.commit()
        await session.refresh(conversation)
        return conversation

    async def _create_remote_dm(
        self,
        session: AsyncSession,
        user: User,
        peer_address: str,
    ) -> Conversation:
        try:
            parsed = parse_peer_address_with_policy(peer_address, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
        local_onion = get_server_onion()
        if parsed.server_onion == local_onion:
            return await self._create_local_dm(session, user, parsed.username)

        existing_stmt = select(Conversation).where(
            Conversation.kind == "remote",
            Conversation.local_user_id == user.id,
            Conversation.peer_address == parsed.canonical,
        )
        existing = (await session.execute(existing_stmt)).scalar_one_or_none()
        if existing is not None:
            return existing

        conversation = Conversation(
            kind="remote",
            local_user_id=user.id,
            peer_username=parsed.username,
            peer_server_onion=parsed.server_onion,
            peer_address=parsed.canonical,
        )
        session.add(conversation)
        await session.commit()
        await session.refresh(conversation)
        return conversation

    async def create_dm(
        self,
        session: AsyncSession,
        user: User,
        peer_username: str | None,
        peer_address: str | None,
    ) -> Conversation:
        if peer_address is not None and peer_address.strip():
            return await self._create_remote_dm(session, user, peer_address)
        if peer_username is not None and peer_username.strip():
            return await self._create_local_dm(session, user, peer_username)
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="peer target is required")

    async def list_for_user(
        self,
        session: AsyncSession,
        user: User,
        limit: int = 50,
        offset: int = 0,
    ) -> list[Conversation]:
        stmt = (
            select(Conversation)
            .where(
                or_(
                    and_(
                        Conversation.kind == "local",
                        or_(Conversation.user_a_id == user.id, Conversation.user_b_id == user.id),
                    ),
                    and_(Conversation.kind == "remote", Conversation.local_user_id == user.id),
                )
            )
            .order_by(Conversation.created_at.desc())
            .offset(offset)
            .limit(limit)
        )
        return list((await session.execute(stmt)).scalars().all())

    async def peer_username_for_user(
        self,
        session: AsyncSession,
        conversation: Conversation,
        user_id: str,
    ) -> str:
        if conversation.kind == "remote":
            return conversation.peer_username or ""

        peer_user_id = self.peer_id(conversation, user_id)
        stmt = select(User.username).where(User.id == peer_user_id)
        username = (await session.execute(stmt)).scalar_one_or_none()
        return username or ""

    async def peer_address_for_user(
        self,
        session: AsyncSession,
        conversation: Conversation,
        user_id: str,
    ) -> str:
        if conversation.kind == "remote":
            return conversation.peer_address or ""
        username = await self.peer_username_for_user(session, conversation, user_id)
        if not username:
            return ""
        return server_address_for_username(username)

    async def peer_server_onion_for_user(
        self,
        session: AsyncSession,
        conversation: Conversation,
        user_id: str,
    ) -> str:
        if conversation.kind == "remote":
            return conversation.peer_server_onion or ""

        peer_address = await self.peer_address_for_user(session, conversation, user_id)
        try:
            return parse_peer_address(peer_address).server_onion
        except ValueError:
            return ""

    def ensure_membership(self, conversation: Conversation, user_id: str) -> None:
        if conversation.kind == "remote":
            if conversation.local_user_id != user_id:
                raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a conversation member")
            return
        if user_id not in (conversation.user_a_id, conversation.user_b_id):
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a conversation member")

    def peer_id(self, conversation: Conversation, user_id: str) -> str:
        if conversation.kind == "remote":
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Remote conversation has no local peer")
        if conversation.user_a_id == user_id:
            if conversation.user_b_id is None:
                raise HTTPException(
                    status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                    detail="Conversation peer missing",
                )
            return conversation.user_b_id
        if conversation.user_b_id == user_id:
            if conversation.user_a_id is None:
                raise HTTPException(
                    status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                    detail="Conversation peer missing",
                )
            return conversation.user_a_id
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a conversation member")

    async def get_or_create_remote_for_local_user(
        self,
        session: AsyncSession,
        local_user: User,
        remote_peer_address: str,
    ) -> Conversation:
        try:
            parsed = parse_peer_address_with_policy(remote_peer_address, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
        existing_stmt = select(Conversation).where(
            Conversation.kind == "remote",
            Conversation.local_user_id == local_user.id,
            Conversation.peer_address == parsed.canonical,
        )
        existing = (await session.execute(existing_stmt)).scalar_one_or_none()
        if existing is not None:
            return existing

        conversation = Conversation(
            kind="remote",
            local_user_id=local_user.id,
            peer_username=parsed.username,
            peer_server_onion=parsed.server_onion,
            peer_address=parsed.canonical,
        )
        session.add(conversation)
        await session.flush()
        return conversation


conversation_service = ConversationService()
