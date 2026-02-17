import asyncio
from collections import defaultdict

from fastapi import WebSocket


class ConnectionManager:
    def __init__(self) -> None:
        self._connections: dict[str, set[WebSocket]] = defaultdict(set)
        self._lock = asyncio.Lock()

    async def connect(self, user_id: str, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections[user_id].add(websocket)

    async def disconnect(self, user_id: str, websocket: WebSocket) -> None:
        async with self._lock:
            sockets = self._connections.get(user_id)
            if sockets is None:
                return
            sockets.discard(websocket)
            if not sockets:
                self._connections.pop(user_id, None)

    async def send_to_user(self, user_id: str, payload: dict) -> int:
        async with self._lock:
            sockets = list(self._connections.get(user_id, set()))

        if not sockets:
            return 0

        sent = 0
        for websocket in sockets:
            try:
                await websocket.send_json(payload)
                sent += 1
            except Exception:
                await self.disconnect(user_id, websocket)
        return sent

    async def has_user(self, user_id: str) -> bool:
        async with self._lock:
            return bool(self._connections.get(user_id))


connection_manager = ConnectionManager()
