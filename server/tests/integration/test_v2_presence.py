from tests.integration.test_v2_security_core import (
    _auth_header,
    _new_device_material,
    _register_device_v2,
    _register_v2_user,
)


def test_v2_presence_set_and_resolve(client) -> None:
    alice_register = _register_v2_user(client, "alice_v2_presence")
    bob_register = _register_v2_user(client, "bob_v2_presence")

    alice_device = _new_device_material("alice-presence-device")
    bob_device = _new_device_material("bob-presence-device")

    alice_tokens = _register_device_v2(client, alice_register["tokens"]["bootstrap_token"], alice_device)["tokens"]
    bob_tokens = _register_device_v2(client, bob_register["tokens"]["bootstrap_token"], bob_device)["tokens"]

    alice_address = "alice_v2_presence@local.invalid"

    offline_resolve = client.post(
        "/api/v2/presence/resolve",
        headers=_auth_header(bob_tokens["access_token"]),
        json={"peer_addresses": [alice_address]},
    )
    assert offline_resolve.status_code == 200, offline_resolve.text
    assert offline_resolve.json()["peers"][0]["status"] == "offline"

    with client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens["access_token"])):
        set_dnd = client.post(
            "/api/v2/presence/set",
            headers=_auth_header(alice_tokens["access_token"]),
            json={"status": "dnd"},
        )
        assert set_dnd.status_code == 200, set_dnd.text
        assert set_dnd.json()["status"] == "dnd"

        resolve_dnd = client.post(
            "/api/v2/presence/resolve",
            headers=_auth_header(bob_tokens["access_token"]),
            json={"peer_addresses": [alice_address]},
        )
        assert resolve_dnd.status_code == 200, resolve_dnd.text
        assert resolve_dnd.json()["peers"][0]["status"] == "dnd"

        set_inactive = client.post(
            "/api/v2/presence/set",
            headers=_auth_header(alice_tokens["access_token"]),
            json={"status": "inactive"},
        )
        assert set_inactive.status_code == 200, set_inactive.text
        assert set_inactive.json()["status"] == "inactive"

        resolve_inactive = client.post(
            "/api/v2/presence/resolve",
            headers=_auth_header(bob_tokens["access_token"]),
            json={"peer_addresses": [alice_address]},
        )
        assert resolve_inactive.status_code == 200, resolve_inactive.text
        assert resolve_inactive.json()["peers"][0]["status"] == "inactive"
