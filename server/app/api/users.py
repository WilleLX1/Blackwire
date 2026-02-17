from fastapi import APIRouter, Depends, HTTPException, Path, Query, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session, get_current_user
from app.models.user import User
from app.schemas.device import DeviceOut, UserDeviceLookup
from app.schemas.user import UserOut
from app.services.device_service import device_service
from app.services.rate_limit import rate_limiter
from app.services.server_identity import server_address_for_username

router = APIRouter(tags=["users"])


@router.get("/me", response_model=UserOut)
async def me(
    request: Request,
    current_user: User = Depends(get_current_user),
) -> UserOut:
    await rate_limiter.enforce(client_rate_limit_key(request, f"me:{current_user.id}"))
    return UserOut.from_user(current_user)


@router.get("/users/{username}/device", response_model=UserDeviceLookup)
async def get_user_device(
    request: Request,
    username: str = Path(pattern=r"^[A-Za-z0-9_]{3,64}$"),
    session: AsyncSession = Depends(db_session),
    current_user: User = Depends(get_current_user),
) -> UserDeviceLookup:
    await rate_limiter.enforce(client_rate_limit_key(request, f"user-device:{current_user.id}"))
    result = await device_service.get_active_device_by_username(session, username)
    if result is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Active device not found")

    user, device = result
    return UserDeviceLookup(
        username=user.username,
        peer_address=server_address_for_username(user.username),
        device=DeviceOut.model_validate(device),
    )


@router.get("/users/resolve-device", response_model=UserDeviceLookup)
async def resolve_user_device(
    request: Request,
    peer_address: str = Query(min_length=3, max_length=320),
    session: AsyncSession = Depends(db_session),
    current_user: User = Depends(get_current_user),
) -> UserDeviceLookup:
    await rate_limiter.enforce(client_rate_limit_key(request, f"user-device-resolve:{current_user.id}"))
    result = await device_service.resolve_device_by_peer_address(session, peer_address)
    if result is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Active device not found")
    return result
