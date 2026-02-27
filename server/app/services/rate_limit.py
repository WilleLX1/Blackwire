import time
from collections import defaultdict
from typing import Any, cast

from fastapi import HTTPException, status

from app.config import get_settings


class RateLimiter:
    def __init__(self) -> None:
        self.settings = get_settings()
        self._memory_counts: dict[str, dict[int, int]] = defaultdict(dict)
        self._redis_client: Any = None

    async def start(self) -> None:
        if not self.settings.use_redis_rate_limit or not self.settings.redis_url:
            return

        try:
            import redis.asyncio as redis_asyncio
        except Exception:  # pragma: no cover
            return

        self._redis_client = redis_asyncio.from_url(self.settings.redis_url, decode_responses=True)

    async def close(self) -> None:
        if self._redis_client is not None:
            await self._redis_client.aclose()
            self._redis_client = None

    async def enforce(self, key: str, limit: int | None = None) -> None:
        current_limit = limit or self.settings.rate_limit_per_minute
        allowed = await self._allow(key, current_limit, units=1)
        if not allowed:
            raise HTTPException(status_code=status.HTTP_429_TOO_MANY_REQUESTS, detail="Rate limit exceeded")

    async def enforce_weighted(self, key: str, units: int, limit: int) -> None:
        effective_units = max(units, 0)
        if effective_units == 0:
            return
        allowed = await self._allow(key, limit, units=effective_units)
        if not allowed:
            raise HTTPException(status_code=status.HTTP_429_TOO_MANY_REQUESTS, detail="Rate limit exceeded")

    async def _allow(self, key: str, limit: int, units: int) -> bool:
        current_window = int(time.time() // 60)
        if self._redis_client is not None:
            redis_key = f"rl:{key}:{current_window}"
            raw_count = await self._redis_client.incrby(redis_key, units)
            count = cast(int, raw_count)
            if count == units:
                await self._redis_client.expire(redis_key, 70)
            return count <= limit

        window_counts = self._memory_counts[key]
        count = window_counts.get(current_window, 0) + units
        window_counts[current_window] = count

        stale_keys = [window for window in window_counts if window < current_window - 1]
        for window in stale_keys:
            window_counts.pop(window, None)

        return count <= limit


rate_limiter = RateLimiter()
