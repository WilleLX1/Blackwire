from fastapi import APIRouter, Depends, Query, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.conversation import ConversationOut, CreateDMConversationRequest
from app.schemas.v2_message import MessageDeviceCopyOutV2, MessageEventOutV2
from app.services.conversation_service import conversation_service
from app.services.message_service_v2 import message_service_v2
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/api/v2/conversations", tags=["conversations-v2"])


@router.post("/dm", response_model=ConversationOut)
async def create_dm(
    payload: CreateDMConversationRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationOut:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-conversation-create:{context.user.id}"))
    conversation = await conversation_service.create_dm(
        session,
        context.user,
        payload.peer_username,
        payload.peer_address,
    )
    peer_username = await conversation_service.peer_username_for_user(session, conversation, context.user.id)
    peer_address = await conversation_service.peer_address_for_user(session, conversation, context.user.id)
    peer_server_onion = await conversation_service.peer_server_onion_for_user(
        session,
        conversation,
        context.user.id,
    )
    return ConversationOut(
        id=conversation.id,
        kind=conversation.kind,
        user_a_id=conversation.user_a_id or "",
        user_b_id=conversation.user_b_id or "",
        local_user_id=conversation.local_user_id or "",
        created_at=conversation.created_at,
        peer_username=peer_username,
        peer_server_onion=peer_server_onion,
        peer_address=peer_address,
    )


@router.get("", response_model=list[ConversationOut])
async def list_conversations(
    request: Request,
    limit: int = Query(default=50, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> list[ConversationOut]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-conversation-list:{context.user.id}"))
    conversations = await conversation_service.list_for_user(session, context.user, limit=limit, offset=offset)
    out: list[ConversationOut] = []
    for item in conversations:
        peer_username = await conversation_service.peer_username_for_user(session, item, context.user.id)
        peer_address = await conversation_service.peer_address_for_user(session, item, context.user.id)
        peer_server_onion = await conversation_service.peer_server_onion_for_user(session, item, context.user.id)
        out.append(
            ConversationOut(
                id=item.id,
                kind=item.kind,
                user_a_id=item.user_a_id or "",
                user_b_id=item.user_b_id or "",
                local_user_id=item.local_user_id or "",
                created_at=item.created_at,
                peer_username=peer_username,
                peer_server_onion=peer_server_onion,
                peer_address=peer_address,
            )
        )
    return out


@router.get("/{conversation_id}/messages", response_model=list[MessageDeviceCopyOutV2])
async def list_messages(
    conversation_id: str,
    request: Request,
    limit: int = Query(default=50, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> list[MessageDeviceCopyOutV2]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-message-list:{context.user.id}"))
    rows = await message_service_v2.list_messages_for_device(
        session,
        context.user,
        context.device.id,
        conversation_id,
        limit=limit,
        offset=offset,
    )
    output: list[MessageDeviceCopyOutV2] = []
    for copy, event in rows:
        output.append(
            MessageDeviceCopyOutV2(
                copy_id=copy.id,
                message=MessageEventOutV2.model_validate(event),
                recipient_device_uid=copy.recipient_device_uid,
                envelope_json=copy.envelope_json,
                status=copy.status,
                created_at=copy.created_at,
            )
        )
    return output

