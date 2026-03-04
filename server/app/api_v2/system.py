from fastapi import APIRouter, Depends

from app.dependencies import AuthenticatedDeviceContextV2, get_current_device_context_v2
from app.schemas.v2_system import SystemVersionOutV2
from app.services.version_info import resolve_server_version_info

router = APIRouter(prefix="/api/v2/system", tags=["system-v2"])


@router.get("/version", response_model=SystemVersionOutV2)
async def version_info(
    _: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> SystemVersionOutV2:
    return SystemVersionOutV2(**resolve_server_version_info())
