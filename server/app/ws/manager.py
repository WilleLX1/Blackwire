import asyncio
from collections import defaultdict
from dataclasses import dataclass

from fastapi import WebSocket


@dataclass(frozen=True, slots=True)
class _ConnectionMeta:
    user_id: str
    device_uid: str | None
    websocket: WebSocket


class ConnectionManager:
    def __init__(self) -> None:
        self._connections: dict[str, set[WebSocket]] = defaultdict(set)
        self._device_connections: dict[str, set[WebSocket]] = defaultdict(set)
        self._reverse_index: dict[WebSocket, _ConnectionMeta] = {}
        self._lock = asyncio.Lock()

    async def connect(self, user_id: str, websocket: WebSocket, device_uid: str | None = None) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections[user_id].add(websocket)
            if device_uid:
                self._device_connections[device_uid].add(websocket)
            self._reverse_index[websocket] = _ConnectionMeta(
                user_id=user_id,
                device_uid=device_uid,
                websocket=websocket,
            )

    async def disconnect(self, user_id: str, websocket: WebSocket, device_uid: str | None = None) -> None:
        async with self._lock:
            meta = self._reverse_index.pop(websocket, None)
            resolved_user_id = user_id or (meta.user_id if meta else "")
            resolved_device_uid = device_uid or (meta.device_uid if meta else None)

            sockets = self._connections.get(resolved_user_id)
            if sockets is None:
                pass
            else:
                sockets.discard(websocket)
                if not sockets:
                    self._connections.pop(resolved_user_id, None)

            if resolved_device_uid:
                device_sockets = self._device_connections.get(resolved_device_uid)
                if device_sockets is not None:
                    device_sockets.discard(websocket)
                    if not device_sockets:
                        self._device_connections.pop(resolved_device_uid, None)

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

    async def send_to_device(self, device_uid: str, payload: dict) -> int:
        async with self._lock:
            sockets = list(self._device_connections.get(device_uid, set()))

        if not sockets:
            return 0

        sent = 0
        for websocket in sockets:
            try:
                await websocket.send_json(payload)
                sent += 1
            except Exception:
                meta = self._reverse_index.get(websocket)
                await self.disconnect(meta.user_id if meta else "", websocket, device_uid=device_uid)
        return sent

    async def disconnect_device(self, device_uid: str) -> int:
        async with self._lock:
            sockets = list(self._device_connections.get(device_uid, set()))

        closed = 0
        for websocket in sockets:
            meta = self._reverse_index.get(websocket)
            try:
                await websocket.close(code=1008, reason="Device revoked")
                closed += 1
            except Exception:
                pass
            await self.disconnect(meta.user_id if meta else "", websocket, device_uid=device_uid)
        return closed

    async def has_user(self, user_id: str) -> bool:
        async with self._lock:
            return bool(self._connections.get(user_id))


connection_manager = ConnectionManager()
