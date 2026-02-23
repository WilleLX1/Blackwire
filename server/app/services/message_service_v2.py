import base64
import hashlib
from datetime import UTC, datetime, timedelta
from typing import Any
from uuid import uuid4

from fastapi import HTTPException, status
from nacl import encoding, signing
from nacl import exceptions as nacl_exceptions
from sqlalchemy import and_, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.device import Device
from app.models.message_device_copy import MessageDeviceCopy
from app.models.message_event import MessageEvent
from app.models.user import User
from app.schemas.v2_federation import FederationMessageRelayRequestV2
from app.schemas.v2_message import MessageSendRequestV2, SignedEnvelopeV2
from app.services.conversation_service import conversation_service
from app.services.device_service_v2 import device_service_v2
from app.services.federation_outbox_service import federation_outbox_service
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_identity import get_server_onion, server_address_for_username
from app.ws.manager import connection_manager


def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _decode_b64(value: str | None) -> bytes:
    if value is None:
        return b""
    return base64.b64decode(value.encode("utf-8"), validate=True)


def canonical_message_signature_string(
    sender_address: str,
    sender_device_uid: str,
    recipient_user_address: str,
    recipient_device_uid: str,
    client_message_id: str,
    sent_at_ms: int,
    sender_prev_hash: str,
    sender_chain_hash: str,
    ciphertext_hash: str,
    aad_hash: str,
) -> bytes:
    canonical = "\n".join(
        [
            sender_address,
            sender_device_uid,
            recipient_user_address,
            recipient_device_uid,
            client_message_id,
            str(sent_at_ms),
            sender_prev_hash,
            sender_chain_hash,
            ciphertext_hash,
            aad_hash,
        ]
    )
    return canonical.encode("utf-8")


class MessageServiceV2:
    def __init__(self) -> None:
        self.settings = get_settings()

    def _validate_envelope_limits(self, envelope: SignedEnvelopeV2) -> None:
        ciphertext_bytes = _decode_b64(envelope.ciphertext_b64)
        if len(ciphertext_bytes) > self.settings.max_ciphertext_bytes:
            raise HTTPException(
                status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                detail="Ciphertext is too large",
            )

        aad_bytes = _decode_b64(envelope.aad_b64)
        if len(aad_bytes) > self.settings.max_aad_bytes:
            raise HTTPException(
                status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                detail="AAD is too large",
            )

    def _verify_signature(
        self,
        *,
        payload: MessageSendRequestV2,
        envelope: SignedEnvelopeV2,
        sender_address: str,
        sender_device_uid: str,
    ) -> tuple[str, str]:
        self._validate_envelope_limits(envelope)
        ciphertext_bytes = _decode_b64(envelope.ciphertext_b64)
        aad_bytes = _decode_b64(envelope.aad_b64)
        ciphertext_hash = _sha256_hex(ciphertext_bytes)
        aad_hash = _sha256_hex(aad_bytes)

        canonical = canonical_message_signature_string(
            sender_address=sender_address,
            sender_device_uid=sender_device_uid,
            recipient_user_address=envelope.recipient_user_address,
            recipient_device_uid=envelope.recipient_device_uid,
            client_message_id=payload.client_message_id,
            sent_at_ms=payload.sent_at_ms,
            sender_prev_hash=payload.sender_prev_hash,
            sender_chain_hash=payload.sender_chain_hash,
            ciphertext_hash=ciphertext_hash,
            aad_hash=aad_hash,
        )
        try:
            verify_key = signing.VerifyKey(
                envelope.sender_device_pubkey.encode("utf-8"),
                encoder=encoding.Base64Encoder,
            )
            signature = encoding.Base64Encoder.decode(envelope.signature_b64.encode("utf-8"))
            verify_key.verify(canonical, signature)
        except (ValueError, nacl_exceptions.BadSignatureError) as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid message signature") from exc
        return ciphertext_hash, aad_hash

    @staticmethod
    def _aggregate_hash(materials: list[str]) -> str:
        joined = "|".join(materials).encode("utf-8")
        return _sha256_hex(joined)

    def _compute_chain_hash(
        self,
        sender_prev_hash: str,
        client_message_id: str,
        sent_at_ms: int,
        envelope_hashes: list[str],
    ) -> str:
        aggregate = self._aggregate_hash(sorted(envelope_hashes))
        chain_data = "\n".join(
            [
                sender_prev_hash,
                client_message_id,
                str(sent_at_ms),
                aggregate,
            ]
        ).encode("utf-8")
        return _sha256_hex(chain_data)

    async def _expected_targets_for_conversation(
        self,
        session: AsyncSession,
        conversation: Conversation,
        sender: User,
        sender_device_uid: str,
    ) -> list[tuple[str, str, str | None]]:
        sender_address = server_address_for_username(sender.username)
        targets: list[tuple[str, str, str | None]] = []

        sender_devices = await device_service_v2.list_active_for_user(session, sender.id)
        for sender_device in sender_devices:
            if sender_device.id == sender_device_uid:
                continue
            targets.append((sender_address, sender_device.id, sender.id))

        if conversation.kind == "local":
            peer_user_id = conversation_service.peer_id(conversation, sender.id)
            peer_stmt = select(User).where(User.id == peer_user_id, User.disabled_at.is_(None))
            peer_user = (await session.execute(peer_stmt)).scalar_one_or_none()
            if peer_user is None:
                raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Peer user not found")
            peer_address = server_address_for_username(peer_user.username)
            peer_devices = await device_service_v2.list_active_for_user(session, peer_user_id)
            for peer_device in peer_devices:
                targets.append((peer_address, peer_device.id, peer_user_id))
            return targets

        if not conversation.peer_address or not conversation.peer_server_onion:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Remote conversation metadata missing")

        remote_resolution = await device_service_v2.resolve_devices_by_peer_address(
            session,
            conversation.peer_address,
        )
        if remote_resolution is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Remote peer devices not found")
        for remote_device in remote_resolution.devices:
            targets.append((remote_resolution.peer_address, remote_device.device_uid, None))
        return targets

    @staticmethod
    def _event_payload(event: MessageEvent, copy: MessageDeviceCopy) -> dict[str, Any]:
        return {
            "type": "message.new",
            "copy_id": copy.id,
            "message": {
                "id": event.id,
                "conversation_id": event.conversation_id,
                "sender_user_id": event.sender_user_id or "",
                "sender_address": event.sender_address,
                "sender_device_uid": event.sender_device_uid,
                "sender_device_pubkey": event.sender_device_pubkey,
                "client_message_id": event.client_message_id,
                "sent_at_ms": event.sent_at_ms,
                "sender_prev_hash": event.sender_prev_hash,
                "sender_chain_hash": event.sender_chain_hash,
                "envelope": copy.envelope_json,
                "created_at": event.created_at.isoformat(),
            },
        }

    async def _deliver_local_copies(self, session: AsyncSession, copies: list[MessageDeviceCopy], event: MessageEvent) -> None:
        delivered_count = 0
        for copy in copies:
            sent = await connection_manager.send_to_device(copy.recipient_device_uid, self._event_payload(event, copy))
            if sent > 0:
                copy.status = "delivered"
                copy.delivered_at = datetime.now(UTC)
                copy.attempt_count += sent
                copy.last_attempt_at = datetime.now(UTC)
                delivered_count += sent
            else:
                copy.attempt_count += 1
                copy.last_attempt_at = datetime.now(UTC)
        if copies:
            await session.commit()
        if delivered_count > 0:
            await metrics.inc("messages.v2.delivered.realtime", delivered_count)

    async def _validate_chain(
        self,
        session: AsyncSession,
        payload: MessageSendRequestV2,
        sender_device_uid: str,
    ) -> None:
        stmt = (
            select(MessageEvent)
            .where(
                MessageEvent.conversation_id == payload.conversation_id,
                MessageEvent.sender_device_uid == sender_device_uid,
            )
            .order_by(MessageEvent.created_at.desc())
            .limit(1)
        )
        previous = (await session.execute(stmt)).scalar_one_or_none()
        expected_prev = previous.sender_chain_hash if previous is not None else ""
        if payload.sender_prev_hash != expected_prev:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="sender_prev_hash does not match latest chain value",
            )

    async def send_message(
        self,
        session: AsyncSession,
        sender: User,
        sender_device: Device,
        payload: MessageSendRequestV2,
    ) -> tuple[MessageEvent, bool]:
        conversation = await conversation_service.get_by_id(session, payload.conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")
        conversation_service.ensure_membership(conversation, sender.id)

        duplicate_stmt = select(MessageEvent).where(
            MessageEvent.sender_device_uid == sender_device.id,
            MessageEvent.client_message_id == payload.client_message_id,
        )
        duplicate = (await session.execute(duplicate_stmt)).scalar_one_or_none()
        if duplicate is not None:
            return duplicate, True

        sender_address = server_address_for_username(sender.username)
        expected_targets = await self._expected_targets_for_conversation(session, conversation, sender, sender_device.id)
        expected_pairs = {(address, device_uid) for address, device_uid, _ in expected_targets}
        envelope_pairs = {(env.recipient_user_address.strip().lower(), env.recipient_device_uid) for env in payload.envelopes}
        if envelope_pairs != expected_pairs:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Envelope recipients do not match expected active device fanout",
            )

        await self._validate_chain(session, payload, sender_device.id)

        envelope_hash_material: list[str] = []
        envelopes_by_target: dict[tuple[str, str], SignedEnvelopeV2] = {}
        for envelope in payload.envelopes:
            if envelope.sender_device_pubkey != sender_device.ik_ed25519_pub:
                raise HTTPException(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    detail="sender_device_pubkey must match sender registered key",
                )
            ciphertext_hash, aad_hash = self._verify_signature(
                payload=payload,
                envelope=envelope,
                sender_address=sender_address,
                sender_device_uid=sender_device.id,
            )
            envelope_hash_material.append(f"{envelope.recipient_device_uid}:{ciphertext_hash}:{aad_hash}")
            key = (envelope.recipient_user_address.strip().lower(), envelope.recipient_device_uid)
            envelopes_by_target[key] = envelope

        expected_chain_hash = self._compute_chain_hash(
            payload.sender_prev_hash,
            payload.client_message_id,
            payload.sent_at_ms,
            envelope_hash_material,
        )
        if expected_chain_hash != payload.sender_chain_hash:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="sender_chain_hash is invalid")

        message_event = MessageEvent(
            conversation_id=conversation.id,
            sender_user_id=sender.id,
            sender_address=sender_address,
            sender_device_uid=sender_device.id,
            sender_device_pubkey=sender_device.ik_ed25519_pub,
            client_message_id=payload.client_message_id,
            sent_at_ms=payload.sent_at_ms,
            sender_prev_hash=payload.sender_prev_hash,
            sender_chain_hash=payload.sender_chain_hash,
        )
        session.add(message_event)
        await session.flush()

        local_copies: list[MessageDeviceCopy] = []
        remote_envelopes: list[dict[str, Any]] = []
        remote_peer_onion = conversation.peer_server_onion if conversation.kind == "remote" else ""
        for address, device_uid, recipient_user_id in expected_targets:
            envelope = envelopes_by_target[(address, device_uid)]
            if recipient_user_id is None:
                remote_envelopes.append(
                    {
                        "recipient_user_address": address,
                        "recipient_device_uid": device_uid,
                        "ciphertext_b64": envelope.ciphertext_b64,
                        "aad_b64": envelope.aad_b64,
                        "signature_b64": envelope.signature_b64,
                        "sender_device_pubkey": envelope.sender_device_pubkey,
                        "recipient_user_id": "",
                    }
                )
                continue

            copy = MessageDeviceCopy(
                message_event_id=message_event.id,
                recipient_user_id=recipient_user_id,
                recipient_device_uid=device_uid,
                envelope_json={
                    "recipient_user_address": address,
                    "recipient_device_uid": device_uid,
                    "ciphertext_b64": envelope.ciphertext_b64,
                    "aad_b64": envelope.aad_b64,
                    "signature_b64": envelope.signature_b64,
                    "sender_device_pubkey": envelope.sender_device_pubkey,
                },
                status="pending",
                expires_at=datetime.now(UTC) + timedelta(days=self.settings.message_ttl_days),
            )
            session.add(copy)
            local_copies.append(copy)

        await session.commit()
        await session.refresh(message_event)
        await metrics.inc("messages.v2.sent")

        if local_copies:
            await self._deliver_local_copies(session, local_copies, message_event)

        if remote_envelopes and remote_peer_onion:
            relay_payload = {
                "relay_id": str(uuid4()),
                "conversation_id": conversation.id,
                "sender_address": sender_address,
                "sender_device_uid": sender_device.id,
                "sender_user_id": sender.id,
                "client_message_id": payload.client_message_id,
                "sent_at_ms": payload.sent_at_ms,
                "sender_prev_hash": payload.sender_prev_hash,
                "sender_chain_hash": payload.sender_chain_hash,
                "envelopes": remote_envelopes,
            }
            dedupe_key = f"v2-message:{sender_device.id}:{payload.client_message_id}:{conversation.peer_address}"
            outbox_item = await federation_outbox_service.enqueue(
                session,
                peer_onion=remote_peer_onion,
                event_type="message.v2.relay",
                endpoint_path="/api/v2/federation/messages/relay",
                payload_json=relay_payload,
                dedupe_key=dedupe_key,
            )
            await session.commit()
            await federation_outbox_service.deliver_item(session, outbox_item.id)

        return message_event, False

    async def list_messages_for_device(
        self,
        session: AsyncSession,
        user: User,
        device_uid: str,
        conversation_id: str,
        limit: int,
        offset: int,
    ) -> list[tuple[MessageDeviceCopy, MessageEvent]]:
        conversation = await conversation_service.get_by_id(session, conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")
        conversation_service.ensure_membership(conversation, user.id)

        stmt = (
            select(MessageDeviceCopy, MessageEvent)
            .join(MessageEvent, MessageEvent.id == MessageDeviceCopy.message_event_id)
            .where(
                MessageDeviceCopy.recipient_user_id == user.id,
                MessageDeviceCopy.recipient_device_uid == device_uid,
                MessageEvent.conversation_id == conversation_id,
            )
            .order_by(MessageEvent.created_at.desc())
            .offset(offset)
            .limit(limit)
        )
        return list((await session.execute(stmt)).all())

    async def drain_pending_for_websocket(
        self,
        session: AsyncSession,
        user_id: str,
        device_uid: str,
        websocket: Any,
    ) -> None:
        await self.expire_old(session)
        now = datetime.now(UTC)
        stmt = (
            select(MessageDeviceCopy, MessageEvent)
            .join(MessageEvent, MessageEvent.id == MessageDeviceCopy.message_event_id)
            .where(
                MessageDeviceCopy.recipient_user_id == user_id,
                MessageDeviceCopy.recipient_device_uid == device_uid,
                MessageDeviceCopy.status == "pending",
                MessageDeviceCopy.available_at <= now,
                MessageDeviceCopy.expires_at > now,
            )
            .order_by(MessageDeviceCopy.available_at.asc())
        )
        rows = list((await session.execute(stmt)).all())
        for copy, event in rows:
            await websocket.send_json(self._event_payload(event, copy))
            copy.attempt_count += 1
            copy.last_attempt_at = datetime.now(UTC)
        if rows:
            await session.commit()

    async def acknowledge(self, session: AsyncSession, user_id: str, device_uid: str, copy_id: str) -> bool:
        stmt = select(MessageDeviceCopy).where(
            MessageDeviceCopy.id == copy_id,
            MessageDeviceCopy.recipient_user_id == user_id,
            MessageDeviceCopy.recipient_device_uid == device_uid,
            MessageDeviceCopy.status == "pending",
        )
        copy = (await session.execute(stmt)).scalar_one_or_none()
        if copy is None:
            return False
        copy.status = "delivered"
        copy.delivered_at = datetime.now(UTC)
        await session.commit()
        await metrics.inc("messages.v2.acknowledged")
        return True

    async def expire_old(self, session: AsyncSession) -> int:
        now = datetime.now(UTC)
        stmt = select(MessageDeviceCopy).where(
            MessageDeviceCopy.status == "pending",
            MessageDeviceCopy.expires_at <= now,
        )
        rows = list((await session.execute(stmt)).scalars().all())
        for row in rows:
            row.status = "expired"
        if rows:
            await session.commit()
            await metrics.inc("messages.v2.expired", len(rows))
        return len(rows)

    async def relay_message_from_federation(
        self,
        session: AsyncSession,
        payload: FederationMessageRelayRequestV2,
    ) -> tuple[MessageEvent, bool]:
        if not payload.envelopes:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Relay payload has no envelopes")

        duplicate_stmt = select(MessageEvent).where(
            MessageEvent.sender_device_uid == payload.sender_device_uid,
            MessageEvent.client_message_id == payload.client_message_id,
        )
        duplicate = (await session.execute(duplicate_stmt)).scalar_one_or_none()
        if duplicate is not None:
            return duplicate, True

        # All relay envelopes must be addressed to the same local user address in v0.2a.
        first_address = payload.envelopes[0].recipient_user_address.strip().lower()
        first_parsed = parse_peer_address_with_policy(first_address, self.settings.tor_enabled)
        if first_parsed.server_onion != get_server_onion():
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Recipient server mismatch")

        user_stmt = select(User).where(User.username == first_parsed.username, User.disabled_at.is_(None))
        recipient_user = (await session.execute(user_stmt)).scalar_one_or_none()
        if recipient_user is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Recipient user not found")

        conversation = await conversation_service.get_or_create_remote_for_local_user(
            session,
            recipient_user,
            payload.sender_address,
        )
        await self._validate_chain(
            session,
            MessageSendRequestV2(
                conversation_id=conversation.id,
                client_message_id=payload.client_message_id,
                sent_at_ms=payload.sent_at_ms,
                sender_prev_hash=payload.sender_prev_hash,
                sender_chain_hash=payload.sender_chain_hash,
                envelopes=[
                    SignedEnvelopeV2(
                        recipient_user_address=envelope.recipient_user_address,
                        recipient_device_uid=envelope.recipient_device_uid,
                        ciphertext_b64=envelope.ciphertext_b64,
                        aad_b64=envelope.aad_b64,
                        signature_b64=envelope.signature_b64,
                        sender_device_pubkey=envelope.sender_device_pubkey,
                    )
                    for envelope in payload.envelopes
                ],
            ),
            payload.sender_device_uid,
        )

        envelope_hash_material: list[str] = []
        local_copies: list[MessageDeviceCopy] = []
        for envelope in payload.envelopes:
            parsed = parse_peer_address_with_policy(envelope.recipient_user_address, self.settings.tor_enabled)
            if parsed.server_onion != get_server_onion() or parsed.username != recipient_user.username:
                raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid relay recipient")
            device_stmt = select(Device).where(
                Device.id == envelope.recipient_device_uid,
                Device.user_id == recipient_user.id,
                Device.status == "active",
                Device.revoked_at.is_(None),
            )
            recipient_device = (await session.execute(device_stmt)).scalar_one_or_none()
            if recipient_device is None:
                raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Recipient device invalid")

            signed_env = SignedEnvelopeV2(
                recipient_user_address=envelope.recipient_user_address,
                recipient_device_uid=envelope.recipient_device_uid,
                ciphertext_b64=envelope.ciphertext_b64,
                aad_b64=envelope.aad_b64,
                signature_b64=envelope.signature_b64,
                sender_device_pubkey=envelope.sender_device_pubkey,
            )
            ciphertext_hash, aad_hash = self._verify_signature(
                payload=MessageSendRequestV2(
                    conversation_id=conversation.id,
                    client_message_id=payload.client_message_id,
                    sent_at_ms=payload.sent_at_ms,
                    sender_prev_hash=payload.sender_prev_hash,
                    sender_chain_hash=payload.sender_chain_hash,
                    envelopes=[signed_env],
                ),
                envelope=signed_env,
                sender_address=payload.sender_address,
                sender_device_uid=payload.sender_device_uid,
            )
            envelope_hash_material.append(
                f"{envelope.recipient_device_uid}:{ciphertext_hash}:{aad_hash}"
            )
            copy = MessageDeviceCopy(
                recipient_user_id=recipient_user.id,
                recipient_device_uid=envelope.recipient_device_uid,
                envelope_json={
                    "recipient_user_address": envelope.recipient_user_address,
                    "recipient_device_uid": envelope.recipient_device_uid,
                    "ciphertext_b64": envelope.ciphertext_b64,
                    "aad_b64": envelope.aad_b64,
                    "signature_b64": envelope.signature_b64,
                    "sender_device_pubkey": envelope.sender_device_pubkey,
                },
                status="pending",
                expires_at=datetime.now(UTC) + timedelta(days=self.settings.message_ttl_days),
                message_event_id="",
            )
            local_copies.append(copy)

        expected_chain_hash = self._compute_chain_hash(
            payload.sender_prev_hash,
            payload.client_message_id,
            payload.sent_at_ms,
            envelope_hash_material,
        )
        if expected_chain_hash != payload.sender_chain_hash:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="sender_chain_hash is invalid")

        event = MessageEvent(
            conversation_id=conversation.id,
            sender_user_id=None,
            sender_address=payload.sender_address,
            sender_device_uid=payload.sender_device_uid,
            sender_device_pubkey=payload.envelopes[0].sender_device_pubkey,
            client_message_id=payload.client_message_id,
            sent_at_ms=payload.sent_at_ms,
            sender_prev_hash=payload.sender_prev_hash,
            sender_chain_hash=payload.sender_chain_hash,
        )
        session.add(event)
        await session.flush()
        for copy in local_copies:
            copy.message_event_id = event.id
            session.add(copy)
        await session.commit()
        await session.refresh(event)

        await self._deliver_local_copies(session, local_copies, event)
        return event, False


message_service_v2 = MessageServiceV2()
