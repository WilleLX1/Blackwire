from fastapi import APIRouter, Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.v2_prekey import PrekeyUploadRequestV2, PrekeyUploadResponseV2
from app.services.prekey_service_v2 import prekey_service_v2
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/api/v2/keys", tags=["keys-v2"])


@router.post("/prekeys/upload", response_model=PrekeyUploadResponseV2)
async def upload_prekeys(
    payload: PrekeyUploadRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> PrekeyUploadResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-prekeys-upload:{context.user.id}"))
    return await prekey_service_v2.upload_prekeys(session, context.user, context.device, payload)
