from fastapi import APIRouter, Depends, Query, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session, get_current_user
from app.models.user import User
from app.schemas.conversation import ConversationOut, CreateDMConversationRequest
from app.schemas.message import MessageOut
from app.services.conversation_service import conversation_service
from app.services.message_service import message_service
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/conversations", tags=["conversations"])


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


@router.post("/dm", response_model=ConversationOut)
async def create_dm(
    payload: CreateDMConversationRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
    current_user: User = Depends(get_current_user),
) -> ConversationOut:
    await rate_limiter.enforce(client_rate_limit_key(request, f"conversation-create:{current_user.id}"))
    conversation = await conversation_service.create_dm(
        session,
        current_user,
        payload.peer_username,
        payload.peer_address,
    )
    peer_username = await conversation_service.peer_username_for_user(session, conversation, current_user.id)
    peer_address = await conversation_service.peer_address_for_user(session, conversation, current_user.id)
    peer_server_onion = await conversation_service.peer_server_onion_for_user(
        session, conversation, current_user.id
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
    current_user: User = Depends(get_current_user),
) -> list[ConversationOut]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"conversation-list:{current_user.id}"))
    conversations = await conversation_service.list_for_user(session, current_user, limit=limit, offset=offset)
    response_items: list[ConversationOut] = []
    for item in conversations:
        peer_username = await conversation_service.peer_username_for_user(session, item, current_user.id)
        peer_address = await conversation_service.peer_address_for_user(session, item, current_user.id)
        peer_server_onion = await conversation_service.peer_server_onion_for_user(
            session, item, current_user.id
        )
        response_items.append(
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
    return response_items


@router.get("/{conversation_id}/messages", response_model=list[MessageOut])
async def list_messages(
    conversation_id: str,
    request: Request,
    limit: int = Query(default=50, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    session: AsyncSession = Depends(db_session),
    current_user: User = Depends(get_current_user),
) -> list[MessageOut]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"message-list:{current_user.id}"))
    messages = await message_service.list_messages(
        session,
        current_user,
        conversation_id=conversation_id,
        limit=limit,
        offset=offset,
    )
    return [_serialize_message(item) for item in messages]
