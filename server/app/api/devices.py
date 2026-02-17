from fastapi import APIRouter, Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session, get_current_user
from app.models.user import User
from app.schemas.device import DeviceOut, DeviceRegisterRequest
from app.services.device_service import device_service
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/devices", tags=["devices"])


@router.post("/register", response_model=DeviceOut)
async def register_device(
    payload: DeviceRegisterRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
    current_user: User = Depends(get_current_user),
) -> DeviceOut:
    await rate_limiter.enforce(client_rate_limit_key(request, f"device-register:{current_user.id}"))
    device = await device_service.register_device(session, current_user, payload)
    return DeviceOut.model_validate(device)
