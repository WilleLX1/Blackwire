import asyncio

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.user import User
from app.services.server_identity import get_server_onion
from app.ws.manager import connection_manager


class PresenceService:
    def __init__(self) -> None:
        self._lock = asyncio.Lock()
        self._manual_status_by_user_id: dict[str, str] = {}

    async def set_status(self, user_id: str, status: str) -> str:
        normalized = status.strip().lower()
        async with self._lock:
            self._manual_status_by_user_id[user_id] = normalized
        return normalized

    async def effective_status(self, user_id: str) -> str:
        async with self._lock:
            manual = self._manual_status_by_user_id.get(user_id, "active")
        if manual == "offline":
            return "offline"
        is_online = await connection_manager.has_user(user_id)
        if not is_online:
            return "offline"
        return manual

    async def resolve_for_peer_addresses(
        self,
        session: AsyncSession,
        peer_addresses: list[str],
    ) -> list[tuple[str, str]]:
        normalized = [address.strip().lower() for address in peer_addresses if address.strip()]
        if not normalized:
            return []

        local_server = get_server_onion().strip().lower()
        usernames: set[str] = set()
        for peer_address in normalized:
            if "@" not in peer_address:
                continue
            username, server_onion = peer_address.split("@", 1)
            if not username or not server_onion:
                continue
            if server_onion != local_server:
                continue
            usernames.add(username)

        by_username: dict[str, User] = {}
        if usernames:
            stmt = select(User).where(User.username.in_(sorted(usernames)), User.disabled_at.is_(None))
            users = (await session.execute(stmt)).scalars().all()
            by_username = {user.username.strip().lower(): user for user in users}

        resolved: list[tuple[str, str]] = []
        for peer_address in normalized:
            if "@" not in peer_address:
                resolved.append((peer_address, "offline"))
                continue
            username, server_onion = peer_address.split("@", 1)
            if not username or not server_onion or server_onion != local_server:
                resolved.append((peer_address, "offline"))
                continue
            user = by_username.get(username)
            if user is None:
                resolved.append((peer_address, "offline"))
                continue
            resolved.append((peer_address, await self.effective_status(user.id)))

        return resolved


presence_service = PresenceService()
