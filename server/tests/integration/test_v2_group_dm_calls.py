import asyncio
import base64
import hashlib
import os
import time
import uuid
from collections.abc import Generator
from pathlib import Path
from typing import Any

import pytest
from fastapi.testclient import TestClient
from nacl import encoding, public, signing

from app.config import get_settings, reset_settings_cache
from app.db import reset_engine
from app.security.tokens_v2 import reset_v2_token_cache


def _auth_header(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _aggregate_chain_hash(
    sender_prev_hash: str,
    client_message_id: str,
    sent_at_ms: int,
    hash_material: list[str],
) -> str:
    aggregate = _sha256_hex("|".join(sorted(hash_material)).encode("utf-8"))
    chain_data = "\n".join([sender_prev_hash, client_message_id, str(sent_at_ms), aggregate]).encode("utf-8")
    return _sha256_hex(chain_data)


def _canonical_message_signature_string(
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


def _new_device_material(label: str) -> dict[str, Any]:
    sign_sk = signing.SigningKey.generate()
    sign_pk_b64 = sign_sk.verify_key.encode(encoder=encoding.Base64Encoder).decode("utf-8")
    dh_sk = public.PrivateKey.generate()
    dh_pk_b64 = _b64(bytes(dh_sk.public_key))
    return {
        "label": label,
        "sign_sk": sign_sk,
        "sign_pk_b64": sign_pk_b64,
        "dh_pk_b64": dh_pk_b64,
    }


def _register_v2_user(client: TestClient, username: str, password: str = "Password123!") -> dict[str, Any]:
    response = client.post("/api/v2/auth/register", json={"username": username, "password": password})
    assert response.status_code == 201, response.text
    return response.json()


def _register_device_v2(client: TestClient, bootstrap_token: str, material: dict[str, Any]) -> dict[str, Any]:
    response = client.post(
        "/api/v2/devices/register",
        headers=_auth_header(bootstrap_token),
        json={
            "label": material["label"],
            "pub_sign_key": material["sign_pk_b64"],
            "pub_dh_key": material["dh_pk_b64"],
        },
    )
    assert response.status_code == 200, response.text
    return response.json()


def _build_group_send_payload(
    *,
    conversation_id: str,
    sender_address: str,
    sender_device_uid: str,
    sender_sign_material: dict[str, Any],
    sender_prev_hash: str,
    targets: list[dict[str, Any]],
    plaintext: bytes,
) -> dict[str, Any]:
    client_message_id = str(uuid.uuid4())
    sent_at_ms = int(time.time() * 1000)
    envelopes: list[dict[str, Any]] = []
    hash_material: list[str] = []
    for target in targets:
        recipient_address = str(target["member_address"]).strip().lower()
        recipient_device_uid = str(target["device"]["device_uid"])
        ciphertext_b64 = _b64(plaintext)
        ciphertext_hash = _sha256_hex(plaintext)
        aad_hash = _sha256_hex(b"")
        hash_material.append(f"{recipient_device_uid}:{ciphertext_hash}:{aad_hash}")
        envelopes.append(
            {
                "recipient_user_address": recipient_address,
                "recipient_device_uid": recipient_device_uid,
                "ciphertext_b64": ciphertext_b64,
                "aad_b64": None,
                "signature_b64": "",
                "sender_device_pubkey": sender_sign_material["sign_pk_b64"],
            }
        )
    sender_chain_hash = _aggregate_chain_hash(sender_prev_hash, client_message_id, sent_at_ms, hash_material)
    for envelope in envelopes:
        canonical = _canonical_message_signature_string(
            sender_address=sender_address,
            sender_device_uid=sender_device_uid,
            recipient_user_address=envelope["recipient_user_address"],
            recipient_device_uid=envelope["recipient_device_uid"],
            client_message_id=client_message_id,
            sent_at_ms=sent_at_ms,
            sender_prev_hash=sender_prev_hash,
            sender_chain_hash=sender_chain_hash,
            ciphertext_hash=_sha256_hex(base64.b64decode(envelope["ciphertext_b64"])),
            aad_hash=_sha256_hex(b""),
        )
        envelope["signature_b64"] = _b64(sender_sign_material["sign_sk"].sign(canonical).signature)
    return {
        "conversation_id": conversation_id,
        "client_message_id": client_message_id,
        "sent_at_ms": sent_at_ms,
        "sender_prev_hash": sender_prev_hash,
        "sender_chain_hash": sender_chain_hash,
        "envelopes": envelopes,
    }


def _receive_until(ws, predicate, max_events: int = 24) -> dict[str, Any]:
    seen: list[str] = []
    for _ in range(max_events):
        event = ws.receive_json()
        seen.append(str(event.get("type", "")))
        if predicate(event):
            return event
    raise AssertionError(f"Expected websocket event not observed; seen={seen}")


@pytest.fixture()
def group_client(tmp_path: Path) -> Generator[TestClient, None, None]:
    db_path = tmp_path / "group_v2.db"
    os.environ["BLACKWIRE_ENVIRONMENT"] = "test"
    os.environ["BLACKWIRE_DATABASE_URL"] = f"sqlite+aiosqlite:///{db_path.as_posix()}"
    os.environ["BLACKWIRE_AUTO_CREATE_TABLES"] = "true"
    os.environ["BLACKWIRE_JWT_SECRET_KEY"] = "test-secret-key-with-at-least-32-bytes"
    os.environ["BLACKWIRE_RATE_LIMIT_PER_MINUTE"] = "10000"
    os.environ["BLACKWIRE_VOICE_CALL_RING_TIMEOUT_SECONDS"] = "2"
    os.environ["BLACKWIRE_TOR_ENABLED"] = "false"
    os.environ["BLACKWIRE_FEDERATION_SERVER_ONION"] = "local.invalid"
    os.environ["BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64"] = ""
    os.environ["BLACKWIRE_ENABLE_GROUP_DM_V2C"] = "true"
    os.environ["BLACKWIRE_ENABLE_GROUP_CALL_V2C"] = "true"

    reset_settings_cache()
    reset_v2_token_cache()
    asyncio.run(reset_engine())

    from app.main import create_app

    app = create_app()
    settings = get_settings()
    from app.services.group_call_service import group_call_service
    from app.services.group_conversation_service import group_conversation_service
    from app.services.message_service_v2 import message_service_v2

    group_conversation_service.settings = settings
    group_call_service.settings = settings
    message_service_v2.settings = settings
    with TestClient(app) as test_client:
        yield test_client

    asyncio.run(reset_engine())
    reset_settings_cache()
    reset_v2_token_cache()


def test_v2_group_dm_no_prejoin_history(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_group_owner")
    bob_register = _register_v2_user(group_client, "bob_group_member")
    carol_register = _register_v2_user(group_client, "carol_group_member")

    alice_material = _new_device_material("alice-group-device")
    bob_material = _new_device_material("bob-group-device")
    carol_material = _new_device_material("carol-group-device")

    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    bob_tokens = _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)["tokens"]
    carol_tokens = _register_device_v2(group_client, carol_register["tokens"]["bootstrap_token"], carol_material)[
        "tokens"
    ]

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"name": "Ops Room", "member_addresses": ["bob_group_member@local.invalid"]},
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]
    assert create_group.json()["conversation_type"] == "group"

    bob_invited = group_client.get(
        f"/api/v2/conversations/{conversation_id}/messages",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_invited.status_code == 403

    bob_accept = group_client.post(
        f"/api/v2/conversations/{conversation_id}/invites/accept",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_accept.status_code == 200, bob_accept.text
    assert bob_accept.json()["status"] == "active"

    recipients_1 = group_client.get(
        f"/api/v2/conversations/{conversation_id}/recipients",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert recipients_1.status_code == 200, recipients_1.text
    targets_1 = recipients_1.json()["recipients"]
    assert len(targets_1) == 1
    assert targets_1[0]["member_address"] == "bob_group_member@local.invalid"

    first_payload = _build_group_send_payload(
        conversation_id=conversation_id,
        sender_address="alice_group_owner@local.invalid",
        sender_device_uid=alice_tokens["device_uid"],
        sender_sign_material=alice_material,
        sender_prev_hash="",
        targets=targets_1,
        plaintext=b"hello-group-before-carol",
    )
    send_1 = group_client.post(
        "/api/v2/messages/send",
        headers=_auth_header(alice_tokens["access_token"]),
        json=first_payload,
    )
    assert send_1.status_code == 200, send_1.text
    first_chain = send_1.json()["message"]["sender_chain_hash"]

    invite_carol = group_client.post(
        f"/api/v2/conversations/{conversation_id}/members/invite",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"member_addresses": ["carol_group_member@local.invalid"]},
    )
    assert invite_carol.status_code == 200, invite_carol.text

    carol_accept = group_client.post(
        f"/api/v2/conversations/{conversation_id}/invites/accept",
        headers=_auth_header(carol_tokens["access_token"]),
    )
    assert carol_accept.status_code == 200, carol_accept.text

    carol_prejoin_history = group_client.get(
        f"/api/v2/conversations/{conversation_id}/messages",
        headers=_auth_header(carol_tokens["access_token"]),
    )
    assert carol_prejoin_history.status_code == 200, carol_prejoin_history.text
    assert carol_prejoin_history.json() == []

    recipients_2 = group_client.get(
        f"/api/v2/conversations/{conversation_id}/recipients",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert recipients_2.status_code == 200, recipients_2.text
    targets_2 = recipients_2.json()["recipients"]
    recipient_addresses = sorted(item["member_address"] for item in targets_2)
    assert recipient_addresses == ["bob_group_member@local.invalid", "carol_group_member@local.invalid"]

    second_payload = _build_group_send_payload(
        conversation_id=conversation_id,
        sender_address="alice_group_owner@local.invalid",
        sender_device_uid=alice_tokens["device_uid"],
        sender_sign_material=alice_material,
        sender_prev_hash=first_chain,
        targets=targets_2,
        plaintext=b"hello-group-after-carol",
    )
    send_2 = group_client.post(
        "/api/v2/messages/send",
        headers=_auth_header(alice_tokens["access_token"]),
        json=second_payload,
    )
    assert send_2.status_code == 200, send_2.text

    carol_messages = group_client.get(
        f"/api/v2/conversations/{conversation_id}/messages",
        headers=_auth_header(carol_tokens["access_token"]),
    )
    assert carol_messages.status_code == 200, carol_messages.text
    assert len(carol_messages.json()) == 1


def test_v2_group_call_offer_join_leave_end(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_group_call")
    bob_register = _register_v2_user(group_client, "bob_group_call")
    alice_material = _new_device_material("alice-call-device")
    bob_material = _new_device_material("bob-call-device")
    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    bob_tokens = _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)["tokens"]

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"name": "Call Room", "member_addresses": ["bob_group_call@local.invalid"]},
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    bob_accept = group_client.post(
        f"/api/v2/conversations/{conversation_id}/invites/accept",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_accept.status_code == 200, bob_accept.text

    with group_client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens["access_token"])) as alice_ws:
        with group_client.websocket_connect("/api/v2/ws", headers=_auth_header(bob_tokens["access_token"])) as bob_ws:
            alice_ws.send_json({"type": "call.offer", "conversation_id": conversation_id})

            incoming = _receive_until(
                bob_ws,
                lambda event: event.get("type") == "call.group.incoming",
            )
            call_id = str(incoming["call_id"])

            bob_ws.send_json({"type": "call.accept", "call_id": call_id})

            active_state = _receive_until(
                alice_ws,
                lambda event: (
                    event.get("type") == "call.group.state"
                    and event.get("call_id") == call_id
                    and event.get("state") == "active"
                ),
            )
            assert active_state["group_uid"] == create_group.json()["group_uid"]

            bob_ws.send_json({"type": "call.end", "call_id": call_id, "reason": "left"})
            bob_ended = _receive_until(
                bob_ws,
                lambda event: event.get("type") == "call.group.ended" and event.get("call_id") == call_id,
            )
            assert bob_ended["reason"] == "left"
            post_leave_state = _receive_until(
                alice_ws,
                lambda event: event.get("type") == "call.group.state" and event.get("call_id") == call_id,
            )
            assert post_leave_state["state"] != "ended"

            # Pool semantics: leaving user can re-enter existing active group call.
            bob_ws.send_json({"type": "call.offer", "conversation_id": conversation_id})
            bob_rejoin_state = _receive_until(
                bob_ws,
                lambda event: (
                    event.get("type") == "call.group.state"
                    and event.get("call_id") == call_id
                    and event.get("state") == "active"
                ),
            )
            assert bob_rejoin_state["group_uid"] == create_group.json()["group_uid"]

            alice_ws.send_json({"type": "call.end", "call_id": call_id, "reason": "ended"})
            ended = _receive_until(
                alice_ws,
                lambda event: event.get("type") == "call.group.ended" and event.get("call_id") == call_id,
            )
            assert ended["reason"] == "ended"


def test_v2_group_call_audio_local_forwarding(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_group_audio")
    bob_register = _register_v2_user(group_client, "bob_group_audio")
    alice_material = _new_device_material("alice-audio-device")
    bob_material = _new_device_material("bob-audio-device")
    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    bob_tokens = _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)["tokens"]

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"name": "Audio Room", "member_addresses": ["bob_group_audio@local.invalid"]},
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    bob_accept = group_client.post(
        f"/api/v2/conversations/{conversation_id}/invites/accept",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_accept.status_code == 200, bob_accept.text

    with group_client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens["access_token"])) as alice_ws:
        with group_client.websocket_connect("/api/v2/ws", headers=_auth_header(bob_tokens["access_token"])) as bob_ws:
            alice_ws.send_json({"type": "call.offer", "conversation_id": conversation_id})
            incoming = _receive_until(
                bob_ws,
                lambda event: event.get("type") == "call.group.incoming",
            )
            call_id = str(incoming["call_id"])

            bob_ws.send_json({"type": "call.accept", "call_id": call_id})
            _receive_until(
                alice_ws,
                lambda event: (
                    event.get("type") == "call.group.state"
                    and event.get("call_id") == call_id
                    and event.get("state") == "active"
                ),
            )

            pcm_b64 = _b64(b"\x00" * 320)
            alice_ws.send_json(
                {
                    "type": "call.audio",
                    "call_id": call_id,
                    "sequence": 1,
                    "pcm_b64": pcm_b64,
                }
            )
            audio_event = _receive_until(
                bob_ws,
                lambda event: event.get("type") == "call.audio" and event.get("call_id") == call_id,
            )
            assert audio_event["pcm_b64"] == pcm_b64


def test_v2_group_rename_pushes_ws_event_to_members(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_group_rename")
    bob_register = _register_v2_user(group_client, "bob_group_rename")

    alice_material = _new_device_material("alice-group-rename-device")
    bob_material = _new_device_material("bob-group-rename-device")

    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    bob_tokens = _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)["tokens"]

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"name": "Before Rename", "member_addresses": ["bob_group_rename@local.invalid"]},
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    bob_accept = group_client.post(
        f"/api/v2/conversations/{conversation_id}/invites/accept",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_accept.status_code == 200, bob_accept.text

    with group_client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens["access_token"])) as alice_ws:
        with group_client.websocket_connect("/api/v2/ws", headers=_auth_header(bob_tokens["access_token"])) as bob_ws:
            rename = group_client.post(
                f"/api/v2/conversations/{conversation_id}/rename",
                headers=_auth_header(alice_tokens["access_token"]),
                json={"name": "After Rename"},
            )
            assert rename.status_code == 200, rename.text
            assert rename.json()["group_name"] == "After Rename"

            alice_event = _receive_until(
                alice_ws,
                lambda event: (
                    event.get("type") == "conversation.group.renamed"
                    and event.get("conversation_id") == conversation_id
                ),
            )
            bob_event = _receive_until(
                bob_ws,
                lambda event: (
                    event.get("type") == "conversation.group.renamed"
                    and event.get("conversation_id") == conversation_id
                ),
            )

            assert alice_event["group_name"] == "After Rename"
            assert bob_event["group_name"] == "After Rename"
            assert alice_event["actor_address"] == "alice_group_rename@local.invalid"
            assert bob_event["actor_address"] == "alice_group_rename@local.invalid"


def test_v2_group_create_skips_owner_alias_and_keeps_invite_capacity(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_group_alias")
    bob_register = _register_v2_user(group_client, "bob_group_alias")
    carol_register = _register_v2_user(group_client, "carol_group_alias")

    alice_material = _new_device_material("alice-group-alias-device")
    bob_material = _new_device_material("bob-group-alias-device")
    carol_material = _new_device_material("carol-group-alias-device")

    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)
    _register_device_v2(group_client, carol_register["tokens"]["bootstrap_token"], carol_material)

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={
            "name": "Alias Safety Room",
            "member_addresses": [
                "alice_group_alias@local.invalid",
                "bob_group_alias@local.invalid",
            ],
        },
    )
    assert create_group.status_code == 200, create_group.text
    payload = create_group.json()
    assert payload["conversation_type"] == "group"
    assert payload["can_manage_members"] is True
    assert payload["member_count"] == 2

    conversation_id = payload["id"]
    members = group_client.get(
        f"/api/v2/conversations/{conversation_id}/members",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert members.status_code == 200, members.text
    member_rows = members.json()
    assert len(member_rows) == 2
    owner_rows = [row for row in member_rows if row["role"] == "owner"]
    assert len(owner_rows) == 1
    assert owner_rows[0]["status"] == "active"

    invite = group_client.post(
        f"/api/v2/conversations/{conversation_id}/members/invite",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"member_addresses": ["carol_group_alias@local.invalid"]},
    )
    assert invite.status_code == 200, invite.text


def test_v2_group_send_allows_sender_fallback_when_no_other_active_targets(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_group_fallback")
    bob_register = _register_v2_user(group_client, "bob_group_fallback")

    alice_material = _new_device_material("alice-group-fallback-device")
    bob_material = _new_device_material("bob-group-fallback-device")

    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"name": "Fallback Room", "member_addresses": ["bob_group_fallback@local.invalid"]},
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    # Bob is invited but not active yet; server should still accept a sender-self fallback copy.
    self_target = [
        {
            "member_address": "alice_group_fallback@local.invalid",
            "device": {"device_uid": alice_tokens["device_uid"]},
        }
    ]
    payload = _build_group_send_payload(
        conversation_id=conversation_id,
        sender_address="alice_group_fallback@local.invalid",
        sender_device_uid=alice_tokens["device_uid"],
        sender_sign_material=alice_material,
        sender_prev_hash="",
        targets=self_target,
        plaintext=b"owner-fallback-message",
    )
    send = group_client.post(
        "/api/v2/messages/send",
        headers=_auth_header(alice_tokens["access_token"]),
        json=payload,
    )
    assert send.status_code == 200, send.text

    alice_messages = group_client.get(
        f"/api/v2/conversations/{conversation_id}/messages",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert alice_messages.status_code == 200, alice_messages.text
    assert len(alice_messages.json()) == 1


def test_v2_group_owner_leave_transfers_to_oldest_eligible_member(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_leave_owner")
    bob_register = _register_v2_user(group_client, "bob_leave_owner")
    carol_register = _register_v2_user(group_client, "carol_leave_owner")

    alice_material = _new_device_material("alice-leave-owner-device")
    bob_material = _new_device_material("bob-leave-owner-device")
    carol_material = _new_device_material("carol-leave-owner-device")

    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    bob_tokens = _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)["tokens"]
    _register_device_v2(group_client, carol_register["tokens"]["bootstrap_token"], carol_material)

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={
            "name": "Owner Transfer Room",
            "member_addresses": ["bob_leave_owner@local.invalid", "carol_leave_owner@local.invalid"],
        },
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    bob_accept = group_client.post(
        f"/api/v2/conversations/{conversation_id}/invites/accept",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_accept.status_code == 200, bob_accept.text

    leave_owner = group_client.post(
        f"/api/v2/conversations/{conversation_id}/leave",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert leave_owner.status_code == 200, leave_owner.text
    assert leave_owner.json()["status"] == "left"

    bob_conversations = group_client.get(
        "/api/v2/conversations",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert bob_conversations.status_code == 200, bob_conversations.text
    matching = [row for row in bob_conversations.json() if row["id"] == conversation_id]
    assert len(matching) == 1
    assert matching[0]["owner_address"] == "bob_leave_owner@local.invalid"
    assert matching[0]["can_manage_members"] is True

    members = group_client.get(
        f"/api/v2/conversations/{conversation_id}/members",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert members.status_code == 200, members.text
    member_by_address = {row["member_address"]: row for row in members.json()}
    assert member_by_address["alice_leave_owner@local.invalid"]["status"] == "left"
    assert member_by_address["alice_leave_owner@local.invalid"]["role"] == "member"
    assert member_by_address["bob_leave_owner@local.invalid"]["status"] == "active"
    assert member_by_address["bob_leave_owner@local.invalid"]["role"] == "owner"


def test_v2_group_owner_leave_promotes_oldest_invited_when_no_active_non_owner(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_leave_invited")
    bob_register = _register_v2_user(group_client, "bob_leave_invited")

    alice_material = _new_device_material("alice-leave-invited-device")
    bob_material = _new_device_material("bob-leave-invited-device")

    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]
    bob_tokens = _register_device_v2(group_client, bob_register["tokens"]["bootstrap_token"], bob_material)["tokens"]

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={
            "name": "Owner Transfer Invited Room",
            "member_addresses": ["bob_leave_invited@local.invalid"],
        },
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    leave_owner = group_client.post(
        f"/api/v2/conversations/{conversation_id}/leave",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert leave_owner.status_code == 200, leave_owner.text
    assert leave_owner.json()["status"] == "left"

    members = group_client.get(
        f"/api/v2/conversations/{conversation_id}/members",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert members.status_code == 200, members.text
    member_by_address = {row["member_address"]: row for row in members.json()}
    assert member_by_address["bob_leave_invited@local.invalid"]["role"] == "owner"
    assert member_by_address["bob_leave_invited@local.invalid"]["status"] == "active"


def test_v2_group_owner_leave_deletes_group_when_no_eligible_non_owner(group_client: TestClient) -> None:
    alice_register = _register_v2_user(group_client, "alice_leave_delete")
    alice_material = _new_device_material("alice-leave-delete-device")
    alice_tokens = _register_device_v2(group_client, alice_register["tokens"]["bootstrap_token"], alice_material)[
        "tokens"
    ]

    create_group = group_client.post(
        "/api/v2/conversations/group",
        headers=_auth_header(alice_tokens["access_token"]),
        json={
            "name": "Owner Delete Room",
            "member_addresses": [],
        },
    )
    assert create_group.status_code == 200, create_group.text
    conversation_id = create_group.json()["id"]

    leave_owner = group_client.post(
        f"/api/v2/conversations/{conversation_id}/leave",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert leave_owner.status_code == 200, leave_owner.text
    assert leave_owner.json()["status"] == "left"

    list_after = group_client.get(
        "/api/v2/conversations",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert list_after.status_code == 200, list_after.text
    assert all(row["id"] != conversation_id for row in list_after.json())

    members = group_client.get(
        f"/api/v2/conversations/{conversation_id}/members",
        headers=_auth_header(alice_tokens["access_token"]),
    )
    assert members.status_code == 404
