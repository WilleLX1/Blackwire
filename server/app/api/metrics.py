from fastapi import APIRouter, Depends, Request

from app.api.utils import client_rate_limit_key
from app.dependencies import get_current_user
from app.models.user import User
from app.services.metrics import metrics
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/metrics", tags=["metrics"])


@router.get("")
async def get_metrics(
    request: Request,
    current_user: User = Depends(get_current_user),
) -> dict[str, dict[str, int]]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"metrics:{current_user.id}"), limit=60)
    return {"counters": await metrics.snapshot()}
