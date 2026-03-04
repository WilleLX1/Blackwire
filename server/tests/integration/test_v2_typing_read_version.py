import base64
import hashlib
import time
import uuid

from app.services.message_service_v2 import canonical_message_signature_string
from app.services.typing_service_v2 import typing_service_v2
from tests.integration.test_v2_security_core import (
    _aggregate_chain_hash,
    _auth_header,
    _new_device_material,
    _register_device_v2,
    _register_v2_user,
)


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _build_dm_send_payload(
    *,
    conversation_id: str,
    sender_address: str,
    sender_device_uid: str,
    sender_sign_material: dict,
    sender_prev_hash: str,
    recipient_address: str,
    recipient_device_uid: str,
    plaintext: bytes,
) -> dict:
    client_message_id = str(uuid.uuid4())
    sent_at_ms = int(time.time() * 1000)
    ciphertext_b64 = _b64(plaintext)
    ciphertext_hash = _sha256_hex(plaintext)
    aad_hash = _sha256_hex(b"")
    sender_chain_hash = _aggregate_chain_hash(
        sender_prev_hash,
        client_message_id,
        sent_at_ms,
        [f"{recipient_device_uid}:{ciphertext_hash}:{aad_hash}"],
    )
    canonical = canonical_message_signature_string(
        sender_address=sender_address,
        sender_device_uid=sender_device_uid,
        recipient_user_address=recipient_address,
        recipient_device_uid=recipient_device_uid,
        client_message_id=client_message_id,
        sent_at_ms=sent_at_ms,
        sender_prev_hash=sender_prev_hash,
        sender_chain_hash=sender_chain_hash,
        ciphertext_hash=ciphertext_hash,
        aad_hash=aad_hash,
    )
    signature_b64 = _b64(sender_sign_material["sign_sk"].sign(canonical).signature)
    return {
        "conversation_id": conversation_id,
        "client_message_id": client_message_id,
        "sent_at_ms": sent_at_ms,
        "sender_prev_hash": sender_prev_hash,
        "sender_chain_hash": sender_chain_hash,
        "envelopes": [
            {
                "recipient_user_address": recipient_address,
                "recipient_device_uid": recipient_device_uid,
                "ciphertext_b64": ciphertext_b64,
                "aad_b64": None,
                "signature_b64": signature_b64,
                "sender_device_pubkey": sender_sign_material["sign_pk_b64"],
            }
        ],
    }


def test_v2_typing_endpoint_rejects_non_member_and_fans_out(client) -> None:
    alice = _register_v2_user(client, "alice_v2_typing")
    bob = _register_v2_user(client, "bob_v2_typing")
    charlie = _register_v2_user(client, "charlie_v2_typing")

    alice_device = _new_device_material("alice-typing-device")
    bob_device = _new_device_material("bob-typing-device")
    charlie_device = _new_device_material("charlie-typing-device")

    alice_tokens = _register_device_v2(client, alice["tokens"]["bootstrap_token"], alice_device)["tokens"]
    bob_tokens = _register_device_v2(client, bob["tokens"]["bootstrap_token"], bob_device)["tokens"]
    charlie_tokens = _register_device_v2(client, charlie["tokens"]["bootstrap_token"], charlie_device)["tokens"]

    dm = client.post(
        "/api/v2/conversations/dm",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"peer_username": "bob_v2_typing"},
    )
    assert dm.status_code == 200, dm.text
    conversation_id = dm.json()["id"]

    with client.websocket_connect("/api/v2/ws", headers=_auth_header(bob_tokens["access_token"])) as bob_ws:
        typing = client.post(
            f"/api/v2/conversations/{conversation_id}/typing",
            headers=_auth_header(alice_tokens["access_token"]),
            json={"state": "on"},
        )
        assert typing.status_code == 200, typing.text
        assert typing.json()["ok"] is True
        assert int(typing.json()["expires_in_ms"]) > 0

        event = bob_ws.receive_json()
        assert event["type"] == "conversation.typing"
        assert event["conversation_id"] == conversation_id
        assert event["from_user_address"] == "alice_v2_typing@local.invalid"
        assert event["state"] == "on"

    non_member = client.post(
        f"/api/v2/conversations/{conversation_id}/typing",
        headers=_auth_header(charlie_tokens["access_token"]),
        json={"state": "off"},
    )
    assert non_member.status_code == 403


def test_v2_typing_endpoint_rejects_when_feature_disabled(client) -> None:
    alice = _register_v2_user(client, "alice_v2_typing_off")
    bob = _register_v2_user(client, "bob_v2_typing_off")
    alice_device = _new_device_material("alice-typing-off-device")
    bob_device = _new_device_material("bob-typing-off-device")

    alice_tokens = _register_device_v2(client, alice["tokens"]["bootstrap_token"], alice_device)["tokens"]
    _register_device_v2(client, bob["tokens"]["bootstrap_token"], bob_device)

    dm = client.post(
        "/api/v2/conversations/dm",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"peer_username": "bob_v2_typing_off"},
    )
    assert dm.status_code == 200, dm.text
    conversation_id = dm.json()["id"]

    previous = typing_service_v2.settings.enable_typing_v03b
    typing_service_v2.settings.enable_typing_v03b = False
    try:
        response = client.post(
            f"/api/v2/conversations/{conversation_id}/typing",
            headers=_auth_header(alice_tokens["access_token"]),
            json={"state": "on"},
        )
        assert response.status_code == 404
    finally:
        typing_service_v2.settings.enable_typing_v03b = previous


def test_v2_read_cursor_monotonic_and_fanout(client) -> None:
    alice = _register_v2_user(client, "alice_v2_read")
    bob = _register_v2_user(client, "bob_v2_read")
    charlie = _register_v2_user(client, "charlie_v2_read")

    alice_device = _new_device_material("alice-read-device")
    bob_device = _new_device_material("bob-read-device")
    charlie_device = _new_device_material("charlie-read-device")

    alice_tokens = _register_device_v2(client, alice["tokens"]["bootstrap_token"], alice_device)["tokens"]
    bob_tokens = _register_device_v2(client, bob["tokens"]["bootstrap_token"], bob_device)["tokens"]
    charlie_tokens = _register_device_v2(client, charlie["tokens"]["bootstrap_token"], charlie_device)["tokens"]

    dm = client.post(
        "/api/v2/conversations/dm",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"peer_username": "bob_v2_read"},
    )
    assert dm.status_code == 200, dm.text
    conversation_id = dm.json()["id"]

    dm_other = client.post(
        "/api/v2/conversations/dm",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"peer_username": "charlie_v2_read"},
    )
    assert dm_other.status_code == 200, dm_other.text
    other_conversation_id = dm_other.json()["id"]

    payload_1 = _build_dm_send_payload(
        conversation_id=conversation_id,
        sender_address="alice_v2_read@local.invalid",
        sender_device_uid=alice_tokens["device_uid"],
        sender_sign_material=alice_device,
        sender_prev_hash="",
        recipient_address="bob_v2_read@local.invalid",
        recipient_device_uid=bob_tokens["device_uid"],
        plaintext=b"first-message",
    )
    send_1 = client.post(
        "/api/v2/messages/send",
        headers=_auth_header(alice_tokens["access_token"]),
        json=payload_1,
    )
    assert send_1.status_code == 200, send_1.text
    first_message = send_1.json()["message"]

    payload_2 = _build_dm_send_payload(
        conversation_id=conversation_id,
        sender_address="alice_v2_read@local.invalid",
        sender_device_uid=alice_tokens["device_uid"],
        sender_sign_material=alice_device,
        sender_prev_hash=first_message["sender_chain_hash"],
        recipient_address="bob_v2_read@local.invalid",
        recipient_device_uid=bob_tokens["device_uid"],
        plaintext=b"second-message",
    )
    send_2 = client.post(
        "/api/v2/messages/send",
        headers=_auth_header(alice_tokens["access_token"]),
        json=payload_2,
    )
    assert send_2.status_code == 200, send_2.text
    second_message = send_2.json()["message"]

    with client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens["access_token"])) as alice_ws:
        read_second = client.post(
            f"/api/v2/conversations/{conversation_id}/read",
            headers=_auth_header(bob_tokens["access_token"]),
            json={
                "last_read_message_id": second_message["id"],
                "last_read_sent_at_ms": second_message["sent_at_ms"],
            },
        )
        assert read_second.status_code == 200, read_second.text
        assert read_second.json()["last_read_message_id"] == second_message["id"]

        event = alice_ws.receive_json()
        assert event["type"] == "conversation.read"
        assert event["conversation_id"] == conversation_id
        assert event["reader_user_address"] == "bob_v2_read@local.invalid"
        assert event["last_read_message_id"] == second_message["id"]
        assert int(event["last_read_sent_at_ms"]) == int(second_message["sent_at_ms"])

    stale = client.post(
        f"/api/v2/conversations/{conversation_id}/read",
        headers=_auth_header(bob_tokens["access_token"]),
        json={
            "last_read_message_id": first_message["id"],
            "last_read_sent_at_ms": first_message["sent_at_ms"],
        },
    )
    assert stale.status_code == 200, stale.text
    assert stale.json()["last_read_message_id"] == second_message["id"]
    assert int(stale.json()["last_read_sent_at_ms"]) == int(second_message["sent_at_ms"])

    idempotent = client.post(
        f"/api/v2/conversations/{conversation_id}/read",
        headers=_auth_header(bob_tokens["access_token"]),
        json={
            "last_read_message_id": second_message["id"],
            "last_read_sent_at_ms": second_message["sent_at_ms"],
        },
    )
    assert idempotent.status_code == 200, idempotent.text
    assert idempotent.json()["last_read_message_id"] == second_message["id"]

    mismatched = client.post(
        f"/api/v2/conversations/{other_conversation_id}/read",
        headers=_auth_header(charlie_tokens["access_token"]),
        json={
            "last_read_message_id": second_message["id"],
            "last_read_sent_at_ms": second_message["sent_at_ms"],
        },
    )
    assert mismatched.status_code == 400

    get_read = client.get(
        f"/api/v2/conversations/{conversation_id}/read",
        headers=_auth_header(bob_tokens["access_token"]),
    )
    assert get_read.status_code == 200, get_read.text
    payload = get_read.json()
    assert payload["conversation_id"] == conversation_id
    assert len(payload["cursors"]) == 1
    assert payload["cursors"][0]["user_address"] == "bob_v2_read@local.invalid"
    assert payload["cursors"][0]["last_read_message_id"] == second_message["id"]


def test_v2_federation_typing_and_read_relays_call_services(client, monkeypatch) -> None:
    calls: list[tuple[str, str]] = []

    async def _fake_verify(request, session):  # noqa: ANN001
        return await request.body()

    async def _fake_relay_typing(session, payload):  # noqa: ANN001
        calls.append(("typing", payload.conversation_id))

    async def _fake_relay_read(session, payload):  # noqa: ANN001
        calls.append(("read", payload.conversation_id))

    monkeypatch.setattr("app.api_v2.federation._verify_federation_write_auth", _fake_verify)
    monkeypatch.setattr("app.api_v2.federation.typing_service_v2.relay_typing_from_federation", _fake_relay_typing)
    monkeypatch.setattr("app.api_v2.federation.read_state_service_v2.relay_read_from_federation", _fake_relay_read)

    typing_response = client.post(
        "/api/v2/federation/conversations/typing",
        json={
            "relay_id": str(uuid.uuid4()),
            "conversation_id": "00000000-0000-0000-0000-000000000123",
            "from_user_address": "alice@example.onion",
            "state": "on",
            "expires_in_ms": 6000,
            "sent_at": "2026-03-01T12:00:00+00:00",
        },
    )
    assert typing_response.status_code == 200, typing_response.text

    read_response = client.post(
        "/api/v2/federation/conversations/read",
        json={
            "relay_id": str(uuid.uuid4()),
            "conversation_id": "00000000-0000-0000-0000-000000000123",
            "reader_user_address": "alice@example.onion",
            "last_read_message_id": "00000000-0000-0000-0000-000000000456",
            "last_read_sent_at_ms": 1710000000000,
            "updated_at": "2026-03-01T12:00:01+00:00",
        },
    )
    assert read_response.status_code == 200, read_response.text
    assert ("typing", "00000000-0000-0000-0000-000000000123") in calls
    assert ("read", "00000000-0000-0000-0000-000000000123") in calls


def test_v2_system_version_endpoint(client) -> None:
    user = _register_v2_user(client, "version_v2_user")
    material = _new_device_material("version-device")
    tokens = _register_device_v2(client, user["tokens"]["bootstrap_token"], material)["tokens"]

    response = client.get(
        "/api/v2/system/version",
        headers=_auth_header(tokens["access_token"]),
    )
    assert response.status_code == 200, response.text
    payload = response.json()
    assert payload["api_version"] == "v2"
    assert isinstance(payload["server_version"], str) and payload["server_version"] != ""
    assert isinstance(payload["git_commit"], str) and payload["git_commit"] != ""
    assert isinstance(payload["build_timestamp"], str) and payload["build_timestamp"] != ""
