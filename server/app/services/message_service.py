import base64
from datetime import UTC, datetime, timedelta
from typing import Any
from uuid import uuid4

from fastapi import HTTPException, status
from sqlalchemy import and_, or_, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.delivery_queue import DeliveryQueue
from app.models.message import Message
from app.models.user import User
from app.schemas.federation import FederationMessageRelayRequest
from app.schemas.message import MessageSendRequest
from app.services.conversation_service import conversation_service
from app.services.device_service import device_service
from app.services.federation_outbox_service import federation_outbox_service
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_identity import get_server_onion, server_address_for_username
from app.ws.manager import connection_manager


class MessageService:
    def __init__(self) -> None:
        self.settings = get_settings()

    @staticmethod
    def _decoded_len(value_b64: str) -> int:
        return len(base64.b64decode(value_b64.encode("utf-8"), validate=True))

    def _validate_envelope_limits(self, payload: MessageSendRequest) -> None:
        ciphertext_bytes = self._decoded_len(payload.envelope.ciphertext_b64)
        if ciphertext_bytes > self.settings.max_ciphertext_bytes:
            raise HTTPException(
                status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                detail="Ciphertext is too large",
            )

        aad_b64 = payload.envelope.aad_b64
        if aad_b64 is not None:
            aad_bytes = self._decoded_len(aad_b64)
            if aad_bytes > self.settings.max_aad_bytes:
                raise HTTPException(
                    status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                    detail="AAD is too large",
                )

    @staticmethod
    def _event_payload(message: Message) -> dict:
        return {
            "type": "message.new",
            "message": {
                "id": message.id,
                "conversation_id": message.conversation_id,
                "sender_user_id": message.sender_user_id or "",
                "sender_address": message.sender_address or "",
                "sender_device_id": message.sender_device_id,
                "client_message_id": message.client_message_id,
                "envelope": message.envelope_json,
                "created_at": message.created_at.isoformat(),
            },
        }

    async def send_message(
        self,
        session: AsyncSession,
        sender: User,
        payload: MessageSendRequest,
    ) -> tuple[Message, bool]:
        self._validate_envelope_limits(payload)

        conversation = await conversation_service.get_by_id(session, payload.conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")
        conversation_service.ensure_membership(conversation, sender.id)

        sender_device = await device_service.get_active_device_for_user(session, sender.id)
        if sender_device is None:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="No active device")

        duplicate_stmt = select(Message).where(
            Message.sender_device_id == sender_device.id,
            Message.client_message_id == payload.envelope.client_message_id,
        )
        duplicate = (await session.execute(duplicate_stmt)).scalar_one_or_none()
        if duplicate is not None:
            return duplicate, True

        sender_address = server_address_for_username(sender.username)

        message = Message(
            conversation_id=conversation.id,
            sender_user_id=sender.id,
            sender_address=sender_address,
            sender_device_id=sender_device.id,
            client_message_id=payload.envelope.client_message_id,
            envelope_json=payload.envelope.model_dump(),
        )
        session.add(message)
        await session.flush()

        outbox_item_id: str | None = None
        peer_user_id: str | None = None
        if conversation.kind == "local":
            peer_user_id = conversation_service.peer_id(conversation, sender.id)
            peer_device = await device_service.get_active_device_for_user(session, peer_user_id)
            if peer_device is None:
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail="Recipient has no active device",
                )

            if payload.envelope.recipient_device_id != peer_device.id:
                raise HTTPException(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    detail="Recipient device does not match active device",
                )

            queue_item = DeliveryQueue(
                message_id=message.id,
                recipient_user_id=peer_user_id,
                status="pending",
                expires_at=datetime.now(UTC) + timedelta(days=self.settings.message_ttl_days),
            )
            session.add(queue_item)
        else:
            if not conversation.peer_address or not conversation.peer_server_onion:
                raise HTTPException(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    detail="Remote conversation metadata is missing",
                )
            relay_payload = {
                "relay_id": str(uuid4()),
                "sender_address": sender_address,
                "sender_device_id": sender_device.id,
                "recipient_address": conversation.peer_address,
                "recipient_device_id": payload.envelope.recipient_device_id,
                "client_message_id": payload.envelope.client_message_id,
                "envelope": payload.envelope.model_dump(),
            }
            dedupe_key = (
                f"message:{sender_device.id}:{payload.envelope.client_message_id}:{conversation.peer_address}"
            )
            outbox_item = await federation_outbox_service.enqueue(
                session,
                peer_onion=conversation.peer_server_onion,
                event_type="message.relay",
                endpoint_path=f"{self.settings.api_prefix}/federation/messages/relay",
                payload_json=relay_payload,
                dedupe_key=dedupe_key,
            )
            outbox_item_id = outbox_item.id

        try:
            await session.commit()
        except IntegrityError:
            await session.rollback()
            duplicate = (
                await session.execute(
                    select(Message).where(
                        Message.sender_device_id == sender_device.id,
                        Message.client_message_id == payload.envelope.client_message_id,
                    )
                )
            ).scalar_one_or_none()
            if duplicate is None:
                raise
            return duplicate, True
        await session.refresh(message)

        await metrics.inc("messages.sent")

        if conversation.kind == "local":
            assert peer_user_id is not None
            delivered = await connection_manager.send_to_user(peer_user_id, self._event_payload(message))
            if delivered > 0:
                queue_lookup = await session.execute(
                    select(DeliveryQueue).where(
                        DeliveryQueue.message_id == message.id,
                        DeliveryQueue.recipient_user_id == peer_user_id,
                    )
                )
                queued = queue_lookup.scalar_one_or_none()
                if queued is not None:
                    queued.status = "delivered"
                    queued.delivered_at = datetime.now(UTC)
                    queued.attempt_count += delivered
                    queued.last_attempt_at = datetime.now(UTC)
                    await session.commit()
                await metrics.inc("messages.delivered.realtime", delivered)
            else:
                await metrics.inc("messages.queued")
        elif outbox_item_id is not None:
            await federation_outbox_service.deliver_item(session, outbox_item_id)

        return message, False

    async def list_messages(
        self,
        session: AsyncSession,
        user: User,
        conversation_id: str,
        limit: int,
        offset: int,
    ) -> list[Message]:
        conversation = await conversation_service.get_by_id(session, conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")
        conversation_service.ensure_membership(conversation, user.id)

        stmt = (
            select(Message)
            .where(Message.conversation_id == conversation_id)
            .order_by(Message.created_at.desc())
            .offset(offset)
            .limit(limit)
        )
        return list((await session.execute(stmt)).scalars().all())

    async def drain_pending_for_websocket(
        self,
        session: AsyncSession,
        user_id: str,
        websocket: Any,
    ) -> None:
        now = datetime.now(UTC)
        await self.expire_old(session)

        stmt = (
            select(DeliveryQueue, Message)
            .join(Message, Message.id == DeliveryQueue.message_id)
            .where(
                DeliveryQueue.recipient_user_id == user_id,
                DeliveryQueue.status == "pending",
                DeliveryQueue.expires_at > now,
                DeliveryQueue.available_at <= now,
            )
            .order_by(DeliveryQueue.available_at.asc())
        )
        rows = (await session.execute(stmt)).all()

        for queue_item, message in rows:
            await websocket.send_json(self._event_payload(message))
            queue_item.attempt_count += 1
            queue_item.last_attempt_at = datetime.now(UTC)

        if rows:
            await session.commit()

    async def acknowledge(self, session: AsyncSession, user_id: str, message_id: str) -> bool:
        stmt = select(DeliveryQueue).where(
            DeliveryQueue.message_id == message_id,
            DeliveryQueue.recipient_user_id == user_id,
            DeliveryQueue.status == "pending",
        )
        queue_item = (await session.execute(stmt)).scalar_one_or_none()
        if queue_item is None:
            return False

        queue_item.status = "delivered"
        queue_item.delivered_at = datetime.now(UTC)
        await session.commit()
        await metrics.inc("messages.acknowledged")
        return True

    async def expire_old(self, session: AsyncSession) -> int:
        now = datetime.now(UTC)
        stmt = select(DeliveryQueue).where(
            DeliveryQueue.status == "pending",
            DeliveryQueue.expires_at <= now,
        )
        rows = list((await session.execute(stmt)).scalars().all())
        for row in rows:
            row.status = "expired"
        if rows:
            await session.commit()
            await metrics.inc("messages.expired", len(rows))
        return len(rows)

    async def undelivered_count(self, session: AsyncSession, user_id: str) -> int:
        now = datetime.now(UTC)
        stmt = select(DeliveryQueue).where(
            and_(DeliveryQueue.recipient_user_id == user_id, DeliveryQueue.status == "pending"),
            or_(DeliveryQueue.expires_at > now, DeliveryQueue.expires_at.is_(None)),
        )
        return len((await session.execute(stmt)).scalars().all())

    async def relay_message_from_federation(
        self,
        session: AsyncSession,
        payload: FederationMessageRelayRequest,
    ) -> tuple[Message, bool]:
        try:
            recipient = parse_peer_address_with_policy(payload.recipient_address, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
        if recipient.server_onion != get_server_onion():
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Recipient server mismatch")

        user_stmt = select(User).where(User.username == recipient.username, User.disabled_at.is_(None))
        recipient_user = (await session.execute(user_stmt)).scalar_one_or_none()
        if recipient_user is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Recipient user not found")

        recipient_device = await device_service.get_active_device_for_user(session, recipient_user.id)
        if recipient_device is None:
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Recipient has no active device")
        if recipient_device.id != payload.recipient_device_id:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Recipient device does not match active device",
            )

        conversation = await conversation_service.get_or_create_remote_for_local_user(
            session,
            recipient_user,
            payload.sender_address,
        )

        duplicate_stmt = select(Message).where(
            Message.sender_device_id == payload.sender_device_id,
            Message.client_message_id == payload.client_message_id,
        )
        duplicate = (await session.execute(duplicate_stmt)).scalar_one_or_none()
        if duplicate is not None:
            return duplicate, True

        message = Message(
            conversation_id=conversation.id,
            sender_user_id=None,
            sender_address=payload.sender_address,
            sender_device_id=payload.sender_device_id,
            client_message_id=payload.client_message_id,
            envelope_json=payload.envelope.model_dump(),
        )
        session.add(message)
        await session.flush()

        queue_item = DeliveryQueue(
            message_id=message.id,
            recipient_user_id=recipient_user.id,
            status="pending",
            expires_at=datetime.now(UTC) + timedelta(days=self.settings.message_ttl_days),
        )
        session.add(queue_item)
        await session.commit()
        await session.refresh(message)

        delivered = await connection_manager.send_to_user(recipient_user.id, self._event_payload(message))
        if delivered > 0:
            queue_item.status = "delivered"
            queue_item.delivered_at = datetime.now(UTC)
            queue_item.attempt_count += delivered
            queue_item.last_attempt_at = datetime.now(UTC)
            await session.commit()

        return message, False


message_service = MessageService()
