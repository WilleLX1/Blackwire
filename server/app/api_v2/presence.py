from fastapi import APIRouter, Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.v2_presence import (
    PresencePeerOut,
    PresenceResolveRequest,
    PresenceResolveResponse,
    PresenceSetRequest,
    PresenceSetResponse,
)
from app.services.presence_service import presence_service
from app.services.rate_limit import rate_limiter

router = APIRouter(tags=["presence-v2"])


@router.post("/api/v2/presence/set", response_model=PresenceSetResponse)
async def set_presence_status(
    payload: PresenceSetRequest,
    request: Request,
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> PresenceSetResponse:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-presence-set:{context.user.id}"))
    status_value = await presence_service.set_status(context.user.id, payload.status)
    return PresenceSetResponse(status=status_value)


@router.post("/api/v2/presence/resolve", response_model=PresenceResolveResponse)
async def resolve_presence_status(
    payload: PresenceResolveRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> PresenceResolveResponse:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-presence-resolve:{context.user.id}"))
    peers = await presence_service.resolve_for_peer_addresses(session, payload.peer_addresses)
    self_status = await presence_service.effective_status(context.user.id)
    return PresenceResolveResponse(
        self_status=self_status,
        peers=[PresencePeerOut(peer_address=peer_address, status=status) for peer_address, status in peers],
    )
