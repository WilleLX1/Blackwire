from fastapi import APIRouter, Depends, HTTPException, Request, status
from pydantic import ValidationError
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.config import get_settings
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.v2_message import MessageEventOutV2, MessageSendRequestV2, MessageSendResponseV2
from app.services.message_service_v2 import message_service_v2
from app.services.metrics import metrics
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/api/v2/messages", tags=["messages-v2"])


@router.post("/send", response_model=MessageSendResponseV2)
async def send_message(
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> MessageSendResponseV2:
    settings = get_settings()
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-message-send:{context.user.id}"))
    raw_body = await request.body()
    if len(raw_body) > settings.max_client_message_body_bytes:
        await metrics.inc("attachments.send.rejected_too_large")
        raise HTTPException(status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE, detail="Message payload too large")
    try:
        await rate_limiter.enforce_weighted(
            f"v2-message-bytes:{context.user.id}",
            units=len(raw_body),
            limit=settings.message_bytes_per_minute_per_user,
        )
    except HTTPException as exc:
        if exc.status_code == status.HTTP_429_TOO_MANY_REQUESTS:
            await metrics.inc("attachments.send.rejected_rate_limited")
        raise
    try:
        payload = MessageSendRequestV2.model_validate_json(raw_body)
    except ValidationError as exc:
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail=exc.errors()) from exc
    try:
        message, duplicate = await message_service_v2.send_message(
            session,
            context.user,
            context.device,
            payload,
            request_authority=request.headers.get("host", "").strip().lower(),
        )
    except HTTPException as exc:
        if exc.status_code == status.HTTP_413_REQUEST_ENTITY_TOO_LARGE:
            await metrics.inc("attachments.send.rejected_too_large")
        raise
    return MessageSendResponseV2(
        duplicate=duplicate,
        message=MessageEventOutV2.model_validate(message),
    )
