import base64
import hashlib
import time
import uuid
from typing import Any

from nacl import encoding, public, signing

from app.services.auth_service_v2 import canonical_bind_device_string
from app.services.message_service_v2 import canonical_message_signature_string


def _auth_header(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


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


def _register_v2_user(client, username: str, password: str = "password123") -> dict[str, Any]:
    response = client.post("/api/v2/auth/register", json={"username": username, "password": password})
    assert response.status_code == 201, response.text
    return response.json()


def _login_v2_user(client, username: str, password: str = "password123") -> dict[str, Any]:
    response = client.post("/api/v2/auth/login", json={"username": username, "password": password})
    assert response.status_code == 200, response.text
    return response.json()


def _register_device_v2(client, bootstrap_token: str, material: dict[str, Any]) -> dict[str, Any]:
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


def _aggregate_chain_hash(sender_prev_hash: str, client_message_id: str, sent_at_ms: int, hash_material: list[str]) -> str:
    aggregate = _sha256_hex("|".join(sorted(hash_material)).encode("utf-8"))
    chain_data = "\n".join([sender_prev_hash, client_message_id, str(sent_at_ms), aggregate]).encode("utf-8")
    return _sha256_hex(chain_data)


def test_v2_bootstrap_bind_refresh_and_revoke(client) -> None:
    register_body = _register_v2_user(client, "alice_v2_auth")
    bootstrap = register_body["tokens"]["bootstrap_token"]

    device1 = _new_device_material("alice-laptop")
    register_device_body = _register_device_v2(client, bootstrap, device1)
    tokens = register_device_body["tokens"]
    device_uid = tokens["device_uid"]

    login_body = _login_v2_user(client, "alice_v2_auth")
    bootstrap_again = login_body["tokens"]["bootstrap_token"]

    nonce = "bindnonce123"
    timestamp_ms = int(time.time() * 1000)
    canonical = canonical_bind_device_string(
        register_body["user"]["id"],
        device_uid,
        nonce,
        timestamp_ms,
    )
    proof_sig = device1["sign_sk"].sign(canonical).signature

    bind = client.post(
        "/api/v2/auth/bind-device",
        headers=_auth_header(bootstrap_again),
        json={
            "device_uid": device_uid,
            "nonce": nonce,
            "timestamp_ms": timestamp_ms,
            "proof_signature_b64": _b64(proof_sig),
        },
    )
    assert bind.status_code == 200, bind.text
    bound_tokens = bind.json()["tokens"]
    assert bound_tokens["device_uid"] == device_uid

    refreshed = client.post(
        "/api/v2/auth/refresh",
        json={"refresh_token": bound_tokens["refresh_token"]},
    )
    assert refreshed.status_code == 200, refreshed.text
    assert refreshed.json()["tokens"]["device_uid"] == device_uid

    revoke = client.post(
        f"/api/v2/devices/{device_uid}/revoke",
        headers=_auth_header(refreshed.json()["tokens"]["access_token"]),
    )
    assert revoke.status_code == 200, revoke.text
    assert revoke.json()["status"] == "revoked"

    me_after_revoke = client.get(
        "/api/v2/me",
        headers=_auth_header(refreshed.json()["tokens"]["access_token"]),
    )
    assert me_after_revoke.status_code == 401


def test_v2_multi_device_signed_fanout_and_ack(client) -> None:
    alice_register = _register_v2_user(client, "alice_v2_msg")
    bob_register = _register_v2_user(client, "bob_v2_msg")

    alice_device_1 = _new_device_material("alice-laptop")
    alice_device_2 = _new_device_material("alice-phone")
    bob_device_1 = _new_device_material("bob-laptop")

    alice_tokens_1 = _register_device_v2(client, alice_register["tokens"]["bootstrap_token"], alice_device_1)["tokens"]
    bob_tokens_1 = _register_device_v2(client, bob_register["tokens"]["bootstrap_token"], bob_device_1)["tokens"]

    alice_login = _login_v2_user(client, "alice_v2_msg")
    alice_tokens_2 = _register_device_v2(client, alice_login["tokens"]["bootstrap_token"], alice_device_2)["tokens"]

    alice_access = alice_tokens_1["access_token"]
    bob_access = bob_tokens_1["access_token"]
    alice_device_1_uid = alice_tokens_1["device_uid"]
    alice_device_2_uid = alice_tokens_2["device_uid"]

    dm = client.post(
        "/api/v2/conversations/dm",
        headers=_auth_header(alice_access),
        json={"peer_username": "bob_v2_msg"},
    )
    assert dm.status_code == 200, dm.text
    conversation_id = dm.json()["id"]

    resolved_peer = client.get(
        "/api/v2/users/resolve-devices",
        headers=_auth_header(alice_access),
        params={"peer_address": "bob_v2_msg@local.invalid"},
    )
    assert resolved_peer.status_code == 200, resolved_peer.text
    bob_devices = resolved_peer.json()["devices"]
    assert len(bob_devices) == 1

    alice_devices = client.get("/api/v2/devices", headers=_auth_header(alice_access))
    assert alice_devices.status_code == 200, alice_devices.text
    active_alice_other = [d for d in alice_devices.json() if d["device_uid"] == alice_device_2_uid]
    assert len(active_alice_other) == 1

    client_message_id = str(uuid.uuid4())
    sent_at_ms = int(time.time() * 1000)
    sender_prev_hash = ""
    sender_sign_pk_b64 = alice_device_1["sign_pk_b64"]

    envelope_targets = [
        ("bob_v2_msg@local.invalid", bob_devices[0]["device_uid"], b"hello-bob"),
        ("alice_v2_msg@local.invalid", alice_device_2_uid, b"self-mirror"),
    ]

    envelopes = []
    hash_material: list[str] = []
    for recipient_address, recipient_device_uid, plaintext in envelope_targets:
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
                "sender_device_pubkey": sender_sign_pk_b64,
            }
        )

    sender_chain_hash = _aggregate_chain_hash(sender_prev_hash, client_message_id, sent_at_ms, hash_material)

    for envelope in envelopes:
        canonical = canonical_message_signature_string(
            sender_address="alice_v2_msg@local.invalid",
            sender_device_uid=alice_device_1_uid,
            recipient_user_address=envelope["recipient_user_address"],
            recipient_device_uid=envelope["recipient_device_uid"],
            client_message_id=client_message_id,
            sent_at_ms=sent_at_ms,
            sender_prev_hash=sender_prev_hash,
            sender_chain_hash=sender_chain_hash,
            ciphertext_hash=_sha256_hex(base64.b64decode(envelope["ciphertext_b64"])),
            aad_hash=_sha256_hex(b""),
        )
        envelope["signature_b64"] = _b64(alice_device_1["sign_sk"].sign(canonical).signature)

    payload = {
        "conversation_id": conversation_id,
        "client_message_id": client_message_id,
        "sent_at_ms": sent_at_ms,
        "sender_prev_hash": sender_prev_hash,
        "sender_chain_hash": sender_chain_hash,
        "envelopes": envelopes,
    }

    with client.websocket_connect("/api/v2/ws", headers=_auth_header(bob_access)) as bob_ws:
        with client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens_2["access_token"])) as alice_ws:
            send = client.post("/api/v2/messages/send", headers=_auth_header(alice_access), json=payload)
            assert send.status_code == 200, send.text
            assert send.json()["duplicate"] is False

            bob_event = bob_ws.receive_json()
            assert bob_event["type"] == "message.new"
            assert bob_event["message"]["sender_device_uid"] == alice_device_1_uid
            bob_ws.send_json({"type": "message.ack", "copy_id": bob_event["copy_id"]})

            alice_mirror_event = alice_ws.receive_json()
            assert alice_mirror_event["type"] == "message.new"
            assert alice_mirror_event["message"]["sender_device_uid"] == alice_device_1_uid
            alice_ws.send_json({"type": "message.ack", "copy_id": alice_mirror_event["copy_id"]})

    duplicate = client.post("/api/v2/messages/send", headers=_auth_header(alice_access), json=payload)
    assert duplicate.status_code == 200
    assert duplicate.json()["duplicate"] is True

