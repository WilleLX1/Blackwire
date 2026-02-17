import asyncio
from collections import Counter


class MetricsRegistry:
    def __init__(self) -> None:
        self._lock = asyncio.Lock()
        self._counters: Counter[str] = Counter()

    async def inc(self, key: str, value: int = 1) -> None:
        async with self._lock:
            self._counters[key] += value

    async def snapshot(self) -> dict[str, int]:
        async with self._lock:
            return dict(self._counters)


metrics = MetricsRegistry()
