import asyncio
import logging
from collections.abc import Awaitable, Callable

from app.config import get_settings

logger = logging.getLogger("blackwire.queue")


async def queue_cleanup_worker(
    cleanup_once: Callable[[], Awaitable[int]],
    stop_event: asyncio.Event,
    interval_seconds: int | None = None,
) -> None:
    settings = get_settings()
    default_interval = max(5, settings.queue_cleanup_interval_seconds)
    interval = max(1, interval_seconds) if interval_seconds is not None else default_interval

    while not stop_event.is_set():
        try:
            expired = await cleanup_once()
            if expired:
                logger.info("queue_cleanup", extra={"expired": expired})
        except Exception:
            logger.exception("queue_cleanup_failed")

        try:
            await asyncio.wait_for(stop_event.wait(), timeout=interval)
        except TimeoutError:
            continue
