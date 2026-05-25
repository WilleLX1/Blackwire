from app.config import get_settings


def _auth_header(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def _register_v2_user(client, username: str, password: str = "Password123!") -> dict:
    response = client.post("/api/v2/auth/register", json={"username": username, "password": password})
    assert response.status_code == 201, response.text
    return response.json()


def _register_v2_device(client, bootstrap_token: str, label: str) -> dict:
    response = client.post(
        "/api/v2/devices/register",
        headers=_auth_header(bootstrap_token),
        json={
            "label": label,
            "pub_sign_key": f"{label}-sign-key-material-0123456789abcdef",
            "pub_dh_key": f"{label}-dh-key-material-0123456789abcdef",
        },
    )
    assert response.status_code == 200, response.text
    return response.json()


def test_v2_create_dm_peer_address_alias_routes_local(client) -> None:
    settings = get_settings()
    previous_aliases = settings.local_server_aliases
    settings.local_server_aliases = "localhost:8000,192.168.1.50:8000"
    try:
        alice = _register_v2_user(client, "alice_alias_dm")
        bob = _register_v2_user(client, "bob_alias_dm")
        alice_device = _register_v2_device(client, alice["tokens"]["bootstrap_token"], "alice-device")
        _register_v2_device(client, bob["tokens"]["bootstrap_token"], "bob-device")

        response = client.post(
            "/api/v2/conversations/dm",
            headers=_auth_header(alice_device["tokens"]["access_token"]),
            json={"peer_address": "bob_alias_dm@localhost:8000"},
        )
    finally:
        settings.local_server_aliases = previous_aliases

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["kind"] == "local"
    assert body["peer_username"] == "bob_alias_dm"
    assert body["peer_address"] == "bob_alias_dm@local.invalid"


def test_v2_resolve_devices_alias_routes_local_without_federation(client, monkeypatch) -> None:
    settings = get_settings()
    previous_aliases = settings.local_server_aliases
    settings.local_server_aliases = "localhost:8000,192.168.1.50:8000"

    async def _unexpected_remote_lookup(_peer_onion: str, _username: str):
        raise AssertionError("remote federation lookup should not be used for local aliases")

    monkeypatch.setattr(
        "app.services.federation_client.federation_client.get_remote_user_devices_v2",
        _unexpected_remote_lookup,
    )

    try:
        alice = _register_v2_user(client, "alice_alias_resolve")
        bob = _register_v2_user(client, "bob_alias_resolve")
        alice_device = _register_v2_device(client, alice["tokens"]["bootstrap_token"], "alice-device")
        _register_v2_device(client, bob["tokens"]["bootstrap_token"], "bob-device")

        response = client.get(
            "/api/v2/users/resolve-devices",
            headers=_auth_header(alice_device["tokens"]["access_token"]),
            params={"peer_address": "bob_alias_resolve@localhost:8000"},
        )
    finally:
        settings.local_server_aliases = previous_aliases

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["username"] == "bob_alias_resolve"
    assert body["attachment_policy_source"] == "local"
