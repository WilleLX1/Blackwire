from fastapi import APIRouter, Depends, HTTPException, Query, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import AuthenticatedDeviceContextV2, db_session, get_current_device_context_v2
from app.schemas.v2_conversation import (
    ConversationReadCursorOutV2,
    ConversationReadRequestV2,
    ConversationReadStateOutV2,
    ConversationMemberOutV2,
    ConversationOutV2,
    ConversationRecipientsOutV2,
    ConversationTypingRequestV2,
    ConversationTypingResponseV2,
    CreateDMConversationRequestV2,
    CreateGroupConversationRequestV2,
    GroupInviteRequestV2,
    GroupLeaveRequestV2,
    GroupRenameRequestV2,
)
from app.schemas.v2_message import MessageDeviceCopyOutV2, MessageEventOutV2
from app.services.conversation_service import conversation_service
from app.services.group_conversation_service import group_conversation_service
from app.services.message_service_v2 import message_service_v2
from app.services.read_state_service_v2 import read_state_service_v2
from app.services.rate_limit import rate_limiter
from app.services.typing_service_v2 import typing_service_v2

router = APIRouter(prefix="/api/v2/conversations", tags=["conversations-v2"])


async def _serialize_conversation(
    session: AsyncSession,
    conversation,
    context: AuthenticatedDeviceContextV2,
) -> ConversationOutV2:
    if conversation.conversation_type == "group":
        return await group_conversation_service.to_conversation_out(
            session,
            conversation=conversation,
            user=context.user,
        )
    peer_username = await conversation_service.peer_username_for_user(session, conversation, context.user.id)
    peer_address = await conversation_service.peer_address_for_user(session, conversation, context.user.id)
    peer_server_onion = await conversation_service.peer_server_onion_for_user(session, conversation, context.user.id)
    return ConversationOutV2(
        id=conversation.id,
        kind=conversation.kind,
        user_a_id=conversation.user_a_id or "",
        user_b_id=conversation.user_b_id or "",
        local_user_id=conversation.local_user_id or "",
        created_at=conversation.created_at,
        peer_username=peer_username,
        peer_server_onion=peer_server_onion,
        peer_address=peer_address,
        conversation_type="direct",
        group_uid=None,
        group_name="",
        member_count=0,
        membership_state="none",
        can_manage_members=False,
        origin_server_onion="",
        owner_address="",
    )


@router.post("/dm", response_model=ConversationOutV2)
async def create_dm(
    payload: CreateDMConversationRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-conversation-create:{context.user.id}"))
    request_authority = request.headers.get("host", "").strip().lower()
    conversation = await conversation_service.create_dm(
        session,
        context.user,
        payload.peer_username,
        payload.peer_address,
        request_authority=request_authority,
    )
    return await _serialize_conversation(session, conversation, context)


@router.post("/group", response_model=ConversationOutV2)
async def create_group(
    payload: CreateGroupConversationRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-create:{context.user.id}"))
    conversation = await group_conversation_service.create_group(
        session,
        context.user,
        name=payload.name,
        member_addresses=payload.member_addresses,
        request_authority=request.headers.get("host", "").strip().lower(),
    )
    return await _serialize_conversation(session, conversation, context)


@router.get("", response_model=list[ConversationOutV2])
async def list_conversations(
    request: Request,
    limit: int = Query(default=50, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> list[ConversationOutV2]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-conversation-list:{context.user.id}"))
    conversations = await conversation_service.list_for_user(session, context.user, limit=limit, offset=offset)
    return [await _serialize_conversation(session, item, context) for item in conversations]


@router.get("/{conversation_id}/members", response_model=list[ConversationMemberOutV2])
async def list_members(
    conversation_id: str,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> list[ConversationMemberOutV2]:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-members:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None or conversation.conversation_type != "group":
        raise HTTPException(status_code=404, detail="Conversation not found")
    members = await group_conversation_service.list_members(session, conversation, context.user.id)
    return [group_conversation_service.member_out(row) for row in members]


@router.post("/{conversation_id}/typing", response_model=ConversationTypingResponseV2)
async def publish_typing(
    conversation_id: str,
    payload: ConversationTypingRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationTypingResponseV2:
    await rate_limiter.enforce(
        client_rate_limit_key(request, f"v2-typing:{context.user.id}"),
        limit=typing_service_v2.settings.typing_event_rate_per_minute,
    )
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None:
        raise HTTPException(status_code=404, detail="Conversation not found")
    expires_in_ms = await typing_service_v2.publish_local_typing(
        session,
        conversation=conversation,
        sender_user=context.user,
        state=payload.state,
    )
    return ConversationTypingResponseV2(ok=True, expires_in_ms=expires_in_ms)


@router.post("/{conversation_id}/read", response_model=ConversationReadCursorOutV2)
async def publish_read(
    conversation_id: str,
    payload: ConversationReadRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationReadCursorOutV2:
    await rate_limiter.enforce(
        client_rate_limit_key(request, f"v2-read-cursor:{context.user.id}"),
        limit=read_state_service_v2.settings.read_cursor_write_rate_per_minute,
    )
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None:
        raise HTTPException(status_code=404, detail="Conversation not found")
    return await read_state_service_v2.publish_local_read_cursor(
        session,
        conversation=conversation,
        reader_user=context.user,
        last_read_message_id=payload.last_read_message_id,
        last_read_sent_at_ms=payload.last_read_sent_at_ms,
    )


@router.get("/{conversation_id}/read", response_model=ConversationReadStateOutV2)
async def get_read(
    conversation_id: str,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationReadStateOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-read-cursor-get:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None:
        raise HTTPException(status_code=404, detail="Conversation not found")
    return await read_state_service_v2.get_read_state(
        session,
        conversation=conversation,
        reader_user=context.user,
    )


@router.post("/{conversation_id}/members/invite", response_model=list[ConversationMemberOutV2])
async def invite_members(
    conversation_id: str,
    payload: GroupInviteRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> list[ConversationMemberOutV2]:
    await rate_limiter.enforce(
        client_rate_limit_key(request, f"v2-group-invite:{context.user.id}"),
        limit=group_conversation_service.settings.group_invite_rate_per_minute,
    )
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None or conversation.conversation_type != "group":
        raise HTTPException(status_code=404, detail="Conversation not found")
    rows = await group_conversation_service.invite_members(
        session,
        conversation=conversation,
        owner=context.user,
        member_addresses=payload.member_addresses,
        request_authority=request.headers.get("host", "").strip().lower(),
    )
    return [group_conversation_service.member_out(row) for row in rows]


@router.post("/{conversation_id}/members/{member_address:path}/remove", response_model=ConversationMemberOutV2)
async def remove_member(
    conversation_id: str,
    member_address: str,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationMemberOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-remove:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None or conversation.conversation_type != "group":
        raise HTTPException(status_code=404, detail="Conversation not found")
    row = await group_conversation_service.remove_member(
        session,
        conversation=conversation,
        owner=context.user,
        member_address=member_address,
    )
    return group_conversation_service.member_out(row)


@router.post("/{conversation_id}/invites/accept", response_model=ConversationMemberOutV2)
async def accept_invite(
    conversation_id: str,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationMemberOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-accept:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None or conversation.conversation_type != "group":
        raise HTTPException(status_code=404, detail="Conversation not found")
    row = await group_conversation_service.accept_invite(session, conversation=conversation, user=context.user)
    return group_conversation_service.member_out(row)


@router.post("/{conversation_id}/leave", response_model=ConversationMemberOutV2)
async def leave_group(
    conversation_id: str,
    request: Request,
    payload: GroupLeaveRequestV2 | None = None,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationMemberOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-leave:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None or conversation.conversation_type != "group":
        raise HTTPException(status_code=404, detail="Conversation not found")
    row = await group_conversation_service.leave(
        session,
        conversation=conversation,
        user=context.user,
        reason=(payload.reason if payload is not None else None),
    )
    return group_conversation_service.member_out(row)


@router.post("/{conversation_id}/rename", response_model=ConversationOutV2)
async def rename_group(
    conversation_id: str,
    payload: GroupRenameRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-rename:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None or conversation.conversation_type != "group":
        raise HTTPException(status_code=404, detail="Conversation not found")
    renamed = await group_conversation_service.rename(session, conversation=conversation, owner=context.user, name=payload.name)
    return await _serialize_conversation(session, renamed, context)


@router.get("/{conversation_id}/recipients", response_model=ConversationRecipientsOutV2)
async def get_recipients(
    conversation_id: str,
    request: Request,
    session: AsyncSession = Depends(db_session),
    context: AuthenticatedDeviceContextV2 = Depends(get_current_device_context_v2),
) -> ConversationRecipientsOutV2:
    await rate_limiter.enforce(client_rate_limit_key(request, f"v2-group-recipients:{context.user.id}"))
    conversation = await conversation_service.get_by_id(session, conversation_id)
    if conversation is None:
        raise HTTPException(status_code=404, detail="Conversation not found")
    if conversation.conversation_type != "group":
        raise HTTPException(status_code=400, detail="Recipients endpoint only applies to group conversations")
    return await group_conversation_service.members_for_recipients(
        session,
        conversation=conversation,
        user=context.user,
        current_device_uid=context.device.id,
    )


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
