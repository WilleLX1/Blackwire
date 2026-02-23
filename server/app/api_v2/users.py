from fastapi import APIRouter, Depends, HTTPException, Query, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.user import UserOut
from app.schemas.v2_device import UserDeviceLookupV2
from app.schemas.v2_prekey import ResolvePrekeysResponseV2
from app.services.device_service_v2 import device_service_v2
from app.services.prekey_service_v2 import prekey_service_v2
from app.services.rate_limit import rate_limiter

router = APIRouter(tags=["users-v2"])


@router.get("/api/v2/me", response_model=UserOut)
async def me(
    request: Request,
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> UserOut:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-me:{context.user.id}"))
    return UserOut.from_user(context.user)


@router.get("/api/v2/users/resolve-devices", response_model=UserDeviceLookupV2)
async def resolve_user_devices(
    request: Request,
    peer_address: str = Query(min_length=3, max_length=320),
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> UserDeviceLookupV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-user-devices:{context.user.id}"))
    result = await device_service_v2.resolve_devices_by_peer_address(session, peer_address)
    if result is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Active devices not found")
    return result


@router.get("/api/v2/users/resolve-prekeys", response_model=ResolvePrekeysResponseV2)
async def resolve_user_prekeys(
    request: Request,
    peer_address: str = Query(min_length=3, max_length=320),
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ResolvePrekeysResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-user-prekeys:{context.user.id}"))
    return await prekey_service_v2.resolve_prekeys(session, context.user, peer_address)
