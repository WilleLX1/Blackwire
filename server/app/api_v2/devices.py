from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_bootstrap_user_v2, get_current_device_context_v2
from app.models.user import User
from app.schemas.user import UserOut
from app.schemas.v2_auth import DeviceAuthResponseV2
from app.schemas.v2_device import DeviceOutV2, DeviceRegisterRequestV2
from app.services.auth_service import AuthServiceError
from app.services.auth_service_v2 import auth_service_v2
from app.services.device_service_v2 import device_service_v2
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/api/v2/devices", tags=["devices-v2"])


@router.post("/register", response_model=DeviceAuthResponseV2)
async def register_device(
    payload: DeviceRegisterRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    bootstrap_user: User = Depends(get_bootstrap_user_v2),
) -> DeviceAuthResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-device-register"))
    try:
        _, tokens = await auth_service_v2.register_device_from_bootstrap(
            session,
            bootstrap_user,
            payload.label,
            payload.pub_sign_key,
            payload.pub_dh_key,
        )
        return DeviceAuthResponseV2(user=UserOut.from_user(bootstrap_user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.get("", response_model=list[DeviceOutV2])
async def list_devices(
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> list[DeviceOutV2]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-device-list:{context.user.id}"))
    return await device_service_v2.list_for_user(session, context.user.id)


@router.post("/{device_uid}/revoke", response_model=DeviceOutV2)
async def revoke_device(
    device_uid: str,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> DeviceOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-device-revoke:{context.user.id}"))
    return await device_service_v2.revoke_device(session, context.user, device_uid)

