import base64
import hashlib
import json
from datetime import UTC, datetime, timedelta
from typing import Any
from uuid import uuid4

from fastapi import HTTPException, status
from nacl import encoding, signing
from nacl import exceptions as nacl_exceptions
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.conversation import Conversation
from app.models.conversation_member import ConversationMember
from app.models.device import Device
from app.models.message_device_copy import MessageDeviceCopy
from app.models.message_event import MessageEvent
from app.models.user import User
from app.schemas.v2_federation import FederationMessageRelayRequestV2
from app.schemas.v2_message import MessageSendRequestV2, SignedEnvelopeV2
from app.services.conversation_service import conversation_service
from app.services.device_service_v2 import device_service_v2
from app.services.federation_outbox_service import federation_outbox_service
from app.services.group_conversation_service import group_conversation_service
from app.services.metrics import metrics
from app.services.peer_address import parse_peer_address_with_policy
from app.services.server_authority import is_local_server_authority
from app.services.server_identity import get_server_onion, server_address_for_username
from app.ws.manager import connection_manager

SEALED_MODE = "sealedbox_v0_2a"
RATCHET_MODE = "ratchet_v0_2b1"


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

    @staticmethod
    def _envelope_storage_bytes(envelope_json: dict[str, Any]) -> int:
        return len(json.dumps(envelope_json, separators=(",", ":"), sort_keys=True).encode("utf-8"))

    async def _pending_queue_usage(self, session: AsyncSession, recipient_device_uid: str) -> tuple[int, int]:
        stmt = select(MessageDeviceCopy).where(
            MessageDeviceCopy.recipient_device_uid == recipient_device_uid,
            MessageDeviceCopy.status == "pending",
        )
        rows = list((await session.execute(stmt)).scalars().all())
        pending_bytes = 0
        for row in rows:
            pending_bytes += self._envelope_storage_bytes(row.envelope_json)
        return len(rows), pending_bytes

    async def _enforce_pending_queue_limits(
        self,
        session: AsyncSession,
        copy_candidates: list[tuple[str, dict[str, Any]]],
    ) -> None:
        usage_by_device: dict[str, tuple[int, int]] = {}
        projected_by_device: dict[str, tuple[int, int]] = {}
        for recipient_device_uid, envelope_json in copy_candidates:
            projected_count, projected_bytes = projected_by_device.get(recipient_device_uid, (0, 0))
            projected_by_device[recipient_device_uid] = (
                projected_count + 1,
                projected_bytes + self._envelope_storage_bytes(envelope_json),
            )

        for recipient_device_uid, (new_count, new_bytes) in projected_by_device.items():
            if recipient_device_uid not in usage_by_device:
                usage_by_device[recipient_device_uid] = await self._pending_queue_usage(session, recipient_device_uid)
            existing_count, existing_bytes = usage_by_device[recipient_device_uid]
            if existing_count + new_count > self.settings.pending_queue_max_copies_per_device:
                await metrics.inc("attachments.send.rejected_queue_pressure")
                raise HTTPException(
                    status_code=status.HTTP_429_TOO_MANY_REQUESTS,
                    detail="Recipient device pending queue copy limit exceeded",
                )
            if existing_bytes + new_bytes > self.settings.pending_queue_max_bytes_per_device:
                await metrics.inc("attachments.send.rejected_queue_pressure")
                raise HTTPException(
                    status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                    detail="Recipient device pending queue byte limit exceeded",
                )

    @staticmethod
    def _validate_encryption_mode(mode: str) -> str:
        normalized = (mode or SEALED_MODE).strip().lower()
        if normalized not in {SEALED_MODE, RATCHET_MODE}:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Unsupported encryption_mode")
        return normalized

    def _enforce_encryption_policy(self, conversation_kind: str, encryption_mode: str) -> None:
        if encryption_mode == RATCHET_MODE and not self.settings.enable_ratchet_v2b1:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="ratchet_v0_2b1 is disabled on this server",
            )

        if conversation_kind == "local" and self.settings.ratchet_require_for_local and encryption_mode != RATCHET_MODE:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="Ratchet mode is required for local delivery",
            )
        if (
            conversation_kind == "remote"
            and self.settings.ratchet_require_for_federation
            and encryption_mode != RATCHET_MODE
        ):
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="Ratchet mode is required for federation delivery",
            )

    @staticmethod
    def _validate_mode_envelope_shape(encryption_mode: str, envelope: SignedEnvelopeV2) -> None:
        if encryption_mode == RATCHET_MODE:
            if envelope.ratchet_header is None:
                raise HTTPException(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    detail="ratchet_header is required for ratchet_v0_2b1",
                )
        else:
            if envelope.ratchet_header is not None or envelope.ratchet_init is not None:
                raise HTTPException(
                    status_code=status.HTTP_400_BAD_REQUEST,
                    detail="ratchet envelope fields are only allowed for ratchet_v0_2b1",
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
        request_authority: str | None = None,
    ) -> list[tuple[str, str, str | None, str | None]]:
        sender_address = server_address_for_username(sender.username)
        targets: list[tuple[str, str, str | None, str | None]] = []

        sender_devices = await device_service_v2.list_active_for_user(session, sender.id)
        for sender_device in sender_devices:
            if sender_device.id == sender_device_uid:
                continue
            targets.append((sender_address, sender_device.id, sender.id, None))

        if conversation.conversation_type == "group":
            await group_conversation_service.ensure_member_access(session, conversation, sender.id, require_active=True)
            member_rows = list(
                (
                    await session.execute(
                        select(ConversationMember).where(
                            ConversationMember.conversation_id == conversation.id,
                            ConversationMember.status == "active",
                        )
                    )
                ).scalars()
            )
            for member in member_rows:
                if member.member_address == sender_address:
                    continue
                lookup = await device_service_v2.resolve_devices_by_peer_address(
                    session,
                    member.member_address,
                    request_authority=request_authority,
                )
                if lookup is None or not lookup.devices:
                    continue
                parsed_remote = parse_peer_address_with_policy(lookup.peer_address, self.settings.tor_enabled)
                additional_aliases = {request_authority} if request_authority else None
                is_local = is_local_server_authority(parsed_remote.server_onion, self.settings, additional_aliases)
                peer_onion = None if is_local else parsed_remote.server_onion
                local_member_user_id = member.member_user_id
                if is_local and local_member_user_id is None:
                    peer_stmt = select(User).where(User.username == parsed_remote.username, User.disabled_at.is_(None))
                    peer_user = (await session.execute(peer_stmt)).scalar_one_or_none()
                    local_member_user_id = None if peer_user is None else peer_user.id
                for device_out in lookup.devices:
                    targets.append(
                        (
                            lookup.peer_address,
                            device_out.device_uid,
                            local_member_user_id if is_local else None,
                            peer_onion,
                        )
                    )
            if not targets:
                targets.append((sender_address, sender_device_uid, sender.id, None))
            return targets

        if conversation.kind == "local":
            peer_user_id = conversation_service.peer_id(conversation, sender.id)
            peer_stmt = select(User).where(User.id == peer_user_id, User.disabled_at.is_(None))
            peer_user = (await session.execute(peer_stmt)).scalar_one_or_none()
            if peer_user is None:
                raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Peer user not found")
            peer_address = server_address_for_username(peer_user.username)
            peer_devices = await device_service_v2.list_active_for_user(session, peer_user_id)
            for peer_device in peer_devices:
                targets.append((peer_address, peer_device.id, peer_user_id, None))
            if not targets:
                targets.append((sender_address, sender_device_uid, sender.id, None))
            return targets

        if not conversation.peer_address or not conversation.peer_server_onion:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Remote conversation metadata missing")

        remote_resolution = await device_service_v2.resolve_devices_by_peer_address(
            session,
            conversation.peer_address,
            request_authority=request_authority,
        )
        if remote_resolution is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Remote peer devices not found")
        try:
            parsed_remote = parse_peer_address_with_policy(remote_resolution.peer_address, self.settings.tor_enabled)
        except ValueError as exc:
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
        additional_aliases = {request_authority} if request_authority else None
        if is_local_server_authority(parsed_remote.server_onion, self.settings, additional_aliases):
            peer_stmt = select(User).where(User.username == parsed_remote.username, User.disabled_at.is_(None))
            peer_user = (await session.execute(peer_stmt)).scalar_one_or_none()
            if peer_user is None:
                raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Peer user not found")
            for local_device in remote_resolution.devices:
                targets.append((remote_resolution.peer_address, local_device.device_uid, peer_user.id, None))
            if not targets:
                targets.append((sender_address, sender_device_uid, sender.id, None))
            return targets
        for remote_device in remote_resolution.devices:
            targets.append((remote_resolution.peer_address, remote_device.device_uid, None, parsed_remote.server_onion))
        if not targets:
            targets.append((sender_address, sender_device_uid, sender.id, None))
        return targets

    async def _canonical_conversation_for_sender(
        self,
        session: AsyncSession,
        conversation: Conversation,
        sender: User,
        request_authority: str | None = None,
    ) -> Conversation:
        if conversation.kind != "remote" or not conversation.peer_address:
            return conversation
        try:
            parsed = parse_peer_address_with_policy(conversation.peer_address, self.settings.tor_enabled)
        except ValueError:
            return conversation
        additional_aliases = {request_authority} if request_authority else None
        if not is_local_server_authority(parsed.server_onion, self.settings, additional_aliases):
            return conversation
        return await conversation_service.create_dm(
            session,
            sender,
            parsed.username,
            None,
            request_authority=request_authority,
        )

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
                "encryption_mode": event.encryption_mode,
                "client_message_id": event.client_message_id,
                "sent_at_ms": event.sent_at_ms,
                "sender_prev_hash": event.sender_prev_hash,
                "sender_chain_hash": event.sender_chain_hash,
                "envelope": copy.envelope_json,
                "created_at": event.created_at.isoformat(),
            },
        }

    async def _deliver_local_copies(
        self, session: AsyncSession, copies: list[MessageDeviceCopy], event: MessageEvent
    ) -> None:
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
        conversation_id: str,
        sender_prev_hash: str,
        sender_device_uid: str,
    ) -> None:
        stmt = (
            select(MessageEvent)
            .where(
                MessageEvent.conversation_id == conversation_id,
                MessageEvent.sender_device_uid == sender_device_uid,
            )
            .order_by(MessageEvent.created_at.desc())
            .limit(1)
        )
        previous = (await session.execute(stmt)).scalar_one_or_none()
        expected_prev = previous.sender_chain_hash if previous is not None else ""
        if sender_prev_hash != expected_prev:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="sender_prev_hash does not match latest chain value",
            )

    async def _has_prior_sender_event(
        self,
        session: AsyncSession,
        conversation_id: str,
        sender_device_uid: str,
    ) -> bool:
        stmt = (
            select(MessageEvent.id)
            .where(
                MessageEvent.conversation_id == conversation_id,
                MessageEvent.sender_device_uid == sender_device_uid,
            )
            .limit(1)
        )
        return (await session.execute(stmt)).scalar_one_or_none() is not None

    async def send_message(
        self,
        session: AsyncSession,
        sender: User,
        sender_device: Device,
        payload: MessageSendRequestV2,
        request_authority: str | None = None,
    ) -> tuple[MessageEvent, bool]:
        conversation = await conversation_service.get_by_id(session, payload.conversation_id)
        if conversation is None:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Conversation not found")
        if conversation.conversation_type == "group":
            await group_conversation_service.ensure_member_access(session, conversation, sender.id, require_active=True)
        else:
            conversation_service.ensure_membership(conversation, sender.id)
        conversation = await self._canonical_conversation_for_sender(
            session,
            conversation,
            sender,
            request_authority=request_authority,
        )
        encryption_mode = self._validate_encryption_mode(payload.encryption_mode)

        duplicate_stmt = select(MessageEvent).where(
            MessageEvent.sender_device_uid == sender_device.id,
            MessageEvent.client_message_id == payload.client_message_id,
        )
        duplicate = (await session.execute(duplicate_stmt)).scalar_one_or_none()
        if duplicate is not None:
            return duplicate, True
        had_prior_event = await self._has_prior_sender_event(session, conversation.id, sender_device.id)

        sender_address = server_address_for_username(sender.username)
        expected_targets = await self._expected_targets_for_conversation(
            session,
            conversation,
            sender,
            sender_device.id,
            request_authority=request_authority,
        )
        policy_scope = "remote" if conversation.kind == "remote" else "local"
        if conversation.conversation_type == "group":
            policy_scope = "remote" if any(peer_onion for _, _, _, peer_onion in expected_targets) else "local"
        self._enforce_encryption_policy(policy_scope, encryption_mode)
        if conversation.conversation_type == "group":
            await metrics.inc("groups.messages.fanout_targets", len(expected_targets))
        expected_pairs = {(address, device_uid) for address, device_uid, _, _ in expected_targets}
        envelope_pairs = {
            (env.recipient_user_address.strip().lower(), env.recipient_device_uid) for env in payload.envelopes
        }
        if envelope_pairs != expected_pairs:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Envelope recipients do not match expected active device fanout",
            )

        await self._validate_chain(session, conversation.id, payload.sender_prev_hash, sender_device.id)

        envelope_hash_material: list[str] = []
        envelopes_by_target: dict[tuple[str, str], SignedEnvelopeV2] = {}
        for envelope in payload.envelopes:
            self._validate_mode_envelope_shape(encryption_mode, envelope)
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
            encryption_mode=encryption_mode,
            sender_prev_hash=payload.sender_prev_hash,
            sender_chain_hash=payload.sender_chain_hash,
        )
        session.add(message_event)
        await session.flush()

        local_copies: list[MessageDeviceCopy] = []
        local_copy_candidates: list[tuple[str, str, dict[str, Any]]] = []
        remote_envelopes_by_peer: dict[str, list[dict[str, Any]]] = {}
        for address, device_uid, recipient_user_id, peer_onion in expected_targets:
            envelope = envelopes_by_target[(address, device_uid)]
            if recipient_user_id is None:
                if not peer_onion:
                    raise HTTPException(
                        status_code=status.HTTP_400_BAD_REQUEST, detail="Remote target missing peer onion"
                    )
                remote_envelopes_by_peer.setdefault(peer_onion, []).append(
                    {
                        "recipient_user_address": address,
                        "recipient_device_uid": device_uid,
                        "ciphertext_b64": envelope.ciphertext_b64,
                        "aad_b64": envelope.aad_b64,
                        "signature_b64": envelope.signature_b64,
                        "sender_device_pubkey": envelope.sender_device_pubkey,
                        "ratchet_header": None
                        if envelope.ratchet_header is None
                        else envelope.ratchet_header.model_dump(),
                        "ratchet_init": None if envelope.ratchet_init is None else envelope.ratchet_init.model_dump(),
                        "recipient_user_id": "",
                    }
                )
                continue

            envelope_json = {
                "recipient_user_address": address,
                "recipient_device_uid": device_uid,
                "ciphertext_b64": envelope.ciphertext_b64,
                "aad_b64": envelope.aad_b64,
                "signature_b64": envelope.signature_b64,
                "sender_device_pubkey": envelope.sender_device_pubkey,
                "ratchet_header": None if envelope.ratchet_header is None else envelope.ratchet_header.model_dump(),
                "ratchet_init": None if envelope.ratchet_init is None else envelope.ratchet_init.model_dump(),
            }
            local_copy_candidates.append(
                (
                    recipient_user_id,
                    device_uid,
                    envelope_json,
                )
            )

        await self._enforce_pending_queue_limits(
            session,
            [(device_uid, envelope_json) for _, device_uid, envelope_json in local_copy_candidates],
        )

        for recipient_user_id, recipient_device_uid, envelope_json in local_copy_candidates:
            copy = MessageDeviceCopy(
                message_event_id=message_event.id,
                recipient_user_id=recipient_user_id,
                recipient_device_uid=recipient_device_uid,
                envelope_json=envelope_json,
                status="pending",
                expires_at=datetime.now(UTC) + timedelta(days=self.settings.message_ttl_days),
            )
            session.add(copy)
            local_copies.append(copy)

        await session.commit()
        await session.refresh(message_event)
        await metrics.inc("messages.v2.sent")
        if encryption_mode == RATCHET_MODE:
            await metrics.inc("messages.v2b1.mode.ratchet.sent")
            if not had_prior_event:
                await metrics.inc("ratchet.session.established")
        elif self.settings.enable_ratchet_v2b1:
            await metrics.inc("messages.v2b1.mode.sealedbox.fallback")

        if local_copies:
            await self._deliver_local_copies(session, local_copies, message_event)

        if remote_envelopes_by_peer:
            relay_payload = {
                "relay_id": str(uuid4()),
                "conversation_id": conversation.id,
                "sender_address": sender_address,
                "sender_device_uid": sender_device.id,
                "sender_user_id": sender.id,
                "encryption_mode": encryption_mode,
                "client_message_id": payload.client_message_id,
                "sent_at_ms": payload.sent_at_ms,
                "sender_prev_hash": payload.sender_prev_hash,
                "sender_chain_hash": payload.sender_chain_hash,
            }
            if conversation.conversation_type == "group" and conversation.group_uid:
                relay_payload["group_uid"] = conversation.group_uid
            for peer_onion, remote_envelopes in remote_envelopes_by_peer.items():
                payload_for_peer = dict(relay_payload)
                payload_for_peer["envelopes"] = remote_envelopes
                dedupe_key = f"v2-message:{sender_device.id}:{payload.client_message_id}:{conversation.id}:{peer_onion}"
                outbox_item = await federation_outbox_service.enqueue(
                    session,
                    peer_onion=peer_onion,
                    event_type="message.v2.relay",
                    endpoint_path="/api/v2/federation/messages/relay",
                    payload_json=payload_for_peer,
                    dedupe_key=dedupe_key,
                )
                await session.commit()
                await federation_outbox_service.deliver_item(session, outbox_item.id)
            if conversation.conversation_type == "group":
                await metrics.inc("groups.messages.relay_servers", len(remote_envelopes_by_peer))

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
        member = None
        if conversation.conversation_type == "group":
            member = await group_conversation_service.ensure_member_access(
                session,
                conversation,
                user.id,
                require_active=True,
            )
        else:
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
        rows: list[tuple[MessageDeviceCopy, MessageEvent]] = [
            (copy, event) for copy, event in (await session.execute(stmt)).all()
        ]
        if conversation.conversation_type == "group" and member is not None and member.joined_at is not None:
            rows = [row for row in rows if row[1].created_at >= member.joined_at]
        return rows

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
        encryption_mode = self._validate_encryption_mode(payload.encryption_mode)
        self._enforce_encryption_policy("remote", encryption_mode)

        duplicate_stmt = select(MessageEvent).where(
            MessageEvent.sender_device_uid == payload.sender_device_uid,
            MessageEvent.client_message_id == payload.client_message_id,
        )
        duplicate = (await session.execute(duplicate_stmt)).scalar_one_or_none()
        if duplicate is not None:
            return duplicate, True
        # Ratchet sessions are tracked by (conversation, sender_device_uid) lifecycle.
        had_prior_event = False

        first_address = payload.envelopes[0].recipient_user_address.strip().lower()
        first_parsed = parse_peer_address_with_policy(first_address, self.settings.tor_enabled)
        if first_parsed.server_onion != get_server_onion():
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Recipient server mismatch")

        conversation: Conversation
        group_member_by_address: dict[str, ConversationMember] = {}
        if payload.group_uid:
            group_conversation = await group_conversation_service.get_group_by_uid(session, payload.group_uid)
            if group_conversation is None:
                raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Group conversation not found")
            conversation = group_conversation
            members = list(
                (
                    await session.execute(
                        select(ConversationMember).where(
                            ConversationMember.conversation_id == conversation.id,
                            ConversationMember.status == "active",
                        )
                    )
                ).scalars()
            )
            group_member_by_address = {row.member_address: row for row in members}
        else:
            user_stmt = select(User).where(User.username == first_parsed.username, User.disabled_at.is_(None))
            recipient_user = (await session.execute(user_stmt)).scalar_one_or_none()
            if recipient_user is None:
                raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Recipient user not found")
            conversation = await conversation_service.get_or_create_remote_for_local_user(
                session,
                recipient_user,
                payload.sender_address,
            )
        had_prior_event = await self._has_prior_sender_event(session, conversation.id, payload.sender_device_uid)
        await self._validate_chain(
            session,
            conversation.id,
            payload.sender_prev_hash,
            payload.sender_device_uid,
        )

        envelope_hash_material: list[str] = []
        local_copies: list[MessageDeviceCopy] = []
        local_copy_candidates: list[tuple[str, str, dict[str, Any]]] = []
        for envelope in payload.envelopes:
            parsed = parse_peer_address_with_policy(envelope.recipient_user_address, self.settings.tor_enabled)
            if parsed.server_onion != get_server_onion():
                raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid relay recipient")
            recipient_user_id = ""
            if payload.group_uid:
                member = group_member_by_address.get(parsed.canonical)
                if member is None or member.member_user_id is None:
                    raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid group relay recipient")
                recipient_user_id = member.member_user_id
            else:
                user_stmt = select(User).where(User.username == parsed.username, User.disabled_at.is_(None))
                recipient_user = (await session.execute(user_stmt)).scalar_one_or_none()
                if recipient_user is None:
                    raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Recipient user not found")
                recipient_user_id = recipient_user.id
            device_stmt = select(Device).where(
                Device.id == envelope.recipient_device_uid,
                Device.user_id == recipient_user_id,
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
                ratchet_header=envelope.ratchet_header,
                ratchet_init=envelope.ratchet_init,
            )
            self._validate_mode_envelope_shape(encryption_mode, signed_env)
            ciphertext_hash, aad_hash = self._verify_signature(
                payload=MessageSendRequestV2(
                    conversation_id=conversation.id,
                    encryption_mode=encryption_mode,
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
            envelope_hash_material.append(f"{envelope.recipient_device_uid}:{ciphertext_hash}:{aad_hash}")
            envelope_json = {
                "recipient_user_address": envelope.recipient_user_address,
                "recipient_device_uid": envelope.recipient_device_uid,
                "ciphertext_b64": envelope.ciphertext_b64,
                "aad_b64": envelope.aad_b64,
                "signature_b64": envelope.signature_b64,
                "sender_device_pubkey": envelope.sender_device_pubkey,
                "ratchet_header": None if envelope.ratchet_header is None else envelope.ratchet_header.model_dump(),
                "ratchet_init": None if envelope.ratchet_init is None else envelope.ratchet_init.model_dump(),
            }
            local_copy_candidates.append((recipient_user_id, envelope.recipient_device_uid, envelope_json))

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
            encryption_mode=encryption_mode,
            sender_prev_hash=payload.sender_prev_hash,
            sender_chain_hash=payload.sender_chain_hash,
        )
        session.add(event)
        await session.flush()
        await self._enforce_pending_queue_limits(
            session, [(device_uid, item) for _, device_uid, item in local_copy_candidates]
        )

        for recipient_user_id, recipient_device_uid, envelope_json in local_copy_candidates:
            copy = MessageDeviceCopy(
                recipient_user_id=recipient_user_id,
                recipient_device_uid=recipient_device_uid,
                envelope_json=envelope_json,
                status="pending",
                expires_at=datetime.now(UTC) + timedelta(days=self.settings.message_ttl_days),
                message_event_id=event.id,
            )
            session.add(copy)
            local_copies.append(copy)
        await session.commit()
        await session.refresh(event)

        await self._deliver_local_copies(session, local_copies, event)
        if encryption_mode == RATCHET_MODE:
            await metrics.inc("messages.v2b1.mode.ratchet.recv")
            if not had_prior_event:
                await metrics.inc("ratchet.session.established")
        elif self.settings.enable_ratchet_v2b1:
            await metrics.inc("messages.v2b1.mode.sealedbox.fallback")
        return event, False


message_service_v2 = MessageServiceV2()
