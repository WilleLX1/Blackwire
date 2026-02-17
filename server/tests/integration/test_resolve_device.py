from tests.helpers import auth_header, register_device, register_user


def test_resolve_device_local_peer_address(client) -> None:
    alice = register_user(client, "alice_resolve")
    bob = register_user(client, "bob_resolve")
    bob_device = register_device(client, bob["tokens"]["access_token"], "bob-device")

    response = client.get(
        "/api/v1/users/resolve-device",
        headers=auth_header(alice["tokens"]["access_token"]),
        params={"peer_address": "bob_resolve@local.invalid"},
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["username"] == "bob_resolve"
    assert body["peer_address"] == "bob_resolve@local.invalid"
    assert body["device"]["id"] == bob_device["id"]
