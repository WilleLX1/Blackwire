from fastapi import APIRouter, Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.v2_message import MessageEventOutV2, MessageSendRequestV2, MessageSendResponseV2
from app.services.message_service_v2 import message_service_v2
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/api/v2/messages", tags=["messages-v2"])


@router.post("/send", response_model=MessageSendResponseV2)
async def send_message(
    payload: MessageSendRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> MessageSendResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-message-send:{context.user.id}"))
    message, duplicate = await message_service_v2.send_message(session, context.user, context.device, payload)
    return MessageSendResponseV2(
        duplicate=duplicate,
        message=MessageEventOutV2.model_validate(message),
    )

