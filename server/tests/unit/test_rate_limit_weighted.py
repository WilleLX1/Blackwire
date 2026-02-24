import pytest
from fastapi import HTTPException

from app.services.rate_limit import RateLimiter


@pytest.mark.asyncio
async def test_weighted_rate_limit_blocks_when_units_exceed_limit() -> None:
    limiter = RateLimiter()
    await limiter.enforce_weighted("weighted:test", units=5, limit=10)
    with pytest.raises(HTTPException) as exc:
        await limiter.enforce_weighted("weighted:test", units=6, limit=10)
    assert exc.value.status_code == 429
