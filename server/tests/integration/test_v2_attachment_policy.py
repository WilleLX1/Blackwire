from datetime import UTC, datetime
from uuid import uuid4

from app.config import get_settings
from app.schemas.v2_device import UserDeviceLookupV2


def _auth_header(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def _register_v2_user(client, username: str, password: str = "password123") -> dict:
    response = client.post("/api/v2/auth/register", json={"username": username, "password": password})
    assert response.status_code == 201, response.text
    return response.json()


def _register_v2_device(client, bootstrap_token: str, label: str) -> dict:
    sign_key = (f"sign-{label}-{uuid4().hex}" * 3)[:64]
    dh_key = (f"dh-{label}-{uuid4().hex}" * 3)[:64]
    response = client.post(
        "/api/v2/devices/register",
        headers=_auth_header(bootstrap_token),
        json={
            "label": label,
            "pub_sign_key": sign_key,
            "pub_dh_key": dh_key,
        },
    )
    assert response.status_code == 200, response.text
    return response.json()


def test_v2_resolve_devices_includes_attachment_policy(client) -> None:
    alice = _register_v2_user(client, "alice_v2_attach_local")
    bob = _register_v2_user(client, "bob_v2_attach_local")
    alice_device = _register_v2_device(client, alice["tokens"]["bootstrap_token"], "alice-device")
    _register_v2_device(client, bob["tokens"]["bootstrap_token"], "bob-device")

    lookup = client.get(
        "/api/v2/users/resolve-devices",
        headers=_auth_header(alice_device["tokens"]["access_token"]),
        params={"peer_address": "bob_v2_attach_local@local.invalid"},
    )
    assert lookup.status_code == 200, lookup.text
    body = lookup.json()
    assert body["attachment_inline_max_bytes"] == get_settings().effective_attachment_inline_max_bytes()
    assert body["max_ciphertext_bytes"] == get_settings().effective_max_ciphertext_bytes()
    assert body["attachment_policy_source"] == "local"


def test_v2_federation_user_devices_exposes_attachment_policy(client) -> None:
    bob = _register_v2_user(client, "bob_v2_attach_federation")
    _register_v2_device(client, bob["tokens"]["bootstrap_token"], "bob-device")

    response = client.get("/api/v2/federation/users/bob_v2_attach_federation/devices")
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["attachment_inline_max_bytes"] == get_settings().effective_attachment_inline_max_bytes()
    assert body["max_ciphertext_bytes"] == get_settings().effective_max_ciphertext_bytes()
    assert body["attachment_policy_source"] == "local"


def test_v2_federation_well_known_exposes_attachment_policy(client) -> None:
    response = client.get("/api/v2/federation/well-known")
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["attachment_inline_max_bytes"] == get_settings().effective_attachment_inline_max_bytes()
    assert body["max_ciphertext_bytes"] == get_settings().effective_max_ciphertext_bytes()
    assert body["attachment_hard_ceiling_bytes"] == get_settings().attachment_hard_ceiling_bytes


def test_v2_resolve_devices_remote_policy_missing_falls_back_local(client, monkeypatch) -> None:
    alice = _register_v2_user(client, "alice_v2_attach_remote")
    alice_device = _register_v2_device(client, alice["tokens"]["bootstrap_token"], "alice-device")

    now = datetime.now(UTC).isoformat()

    async def _fake_remote_lookup(_peer_onion: str, username: str) -> UserDeviceLookupV2:
        return UserDeviceLookupV2.model_validate(
            {
                "username": username,
                "peer_address": f"{username}@remote.invalid",
                "devices": [
                    {
                        "device_uid": "remote-device-1",
                        "user_id": "remote-user-1",
                        "label": "remote",
                        "pub_sign_key": "C" * 44,
                        "pub_dh_key": "D" * 44,
                        "status": "active",
                        "supported_message_modes": ["sealedbox_v0_2a"],
                        "created_at": now,
                        "last_seen_at": now,
                        "revoked_at": None,
                    }
                ],
                "attachment_inline_max_bytes": 0,
                "max_ciphertext_bytes": 0,
                "attachment_policy_source": "remote",
            }
        )

    monkeypatch.setattr(
        "app.services.federation_client.federation_client.get_remote_user_devices_v2",
        _fake_remote_lookup,
    )

    response = client.get(
        "/api/v2/users/resolve-devices",
        headers=_auth_header(alice_device["tokens"]["access_token"]),
        params={"peer_address": "bob_remote@remote.invalid"},
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["attachment_policy_source"] == "fallback_local"
    assert body["attachment_inline_max_bytes"] == get_settings().effective_attachment_inline_max_bytes()
    assert body["max_ciphertext_bytes"] == get_settings().effective_max_ciphertext_bytes()


def test_v2_messages_send_rejects_payload_above_body_cap(client) -> None:
    alice = _register_v2_user(client, "alice_v2_attach_body")
    alice_device = _register_v2_device(client, alice["tokens"]["bootstrap_token"], "alice-device")

    settings = get_settings()
    original_cap = settings.max_client_message_body_bytes
    settings.max_client_message_body_bytes = 128
    try:
        payload = '{"conversation_id":"' + ("x" * 256) + '"}'
        response = client.post(
            "/api/v2/messages/send",
            headers={
                **_auth_header(alice_device["tokens"]["access_token"]),
                "Content-Type": "application/json",
            },
            content=payload.encode("utf-8"),
        )
    finally:
        settings.max_client_message_body_bytes = original_cap

    assert response.status_code == 413, response.text
