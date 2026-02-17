from fastapi import APIRouter, Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session, get_current_user
from app.models.user import User
from app.schemas.message import MessageOut, MessageSendRequest, MessageSendResponse
from app.services.message_service import message_service
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/messages", tags=["messages"])


def _serialize_message(item) -> MessageOut:
    return MessageOut(
        id=item.id,
        conversation_id=item.conversation_id,
        sender_user_id=item.sender_user_id or "",
        sender_address=item.sender_address or "",
        sender_device_id=item.sender_device_id,
        client_message_id=item.client_message_id,
        envelope_json=item.envelope_json,
        created_at=item.created_at,
    )


@router.post("/send", response_model=MessageSendResponse)
async def send_message(
    payload: MessageSendRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
    current_user: User = Depends(get_current_user),
) -> MessageSendResponse:
    await rate_limiter.enforce(client_rate_limit_key(request, f"message-send:{current_user.id}"))
    message, duplicate = await message_service.send_message(session, current_user, payload)
    return MessageSendResponse(
        duplicate=duplicate,
        message=_serialize_message(message),
    )
