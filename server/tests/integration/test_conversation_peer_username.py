from tests.helpers import auth_header, register_user


def test_conversation_responses_include_peer_username(client) -> None:
    alice = register_user(client, "alice_peername")
    bob = register_user(client, "bob_peername")

    alice_token = alice["tokens"]["access_token"]
    bob_token = bob["tokens"]["access_token"]

    create_for_alice = client.post(
        "/api/v1/conversations/dm",
        headers=auth_header(alice_token),
        json={"peer_username": "bob_peername"},
    )
    assert create_for_alice.status_code == 200, create_for_alice.text
    assert create_for_alice.json()["peer_username"] == "bob_peername"
    assert create_for_alice.json()["peer_address"] == "bob_peername@local.invalid"
    assert create_for_alice.json()["peer_server_onion"] == "local.invalid"

    create_for_bob = client.post(
        "/api/v1/conversations/dm",
        headers=auth_header(bob_token),
        json={"peer_username": "alice_peername"},
    )
    assert create_for_bob.status_code == 200, create_for_bob.text
    assert create_for_bob.json()["peer_username"] == "alice_peername"
    assert create_for_bob.json()["peer_address"] == "alice_peername@local.invalid"
    assert create_for_bob.json()["peer_server_onion"] == "local.invalid"

    list_for_alice = client.get("/api/v1/conversations", headers=auth_header(alice_token))
    assert list_for_alice.status_code == 200, list_for_alice.text
    assert len(list_for_alice.json()) == 1
    assert list_for_alice.json()[0]["peer_username"] == "bob_peername"
    assert list_for_alice.json()[0]["peer_address"] == "bob_peername@local.invalid"

    list_for_bob = client.get("/api/v1/conversations", headers=auth_header(bob_token))
    assert list_for_bob.status_code == 200, list_for_bob.text
    assert len(list_for_bob.json()) == 1
    assert list_for_bob.json()[0]["peer_username"] == "alice_peername"
    assert list_for_bob.json()[0]["peer_address"] == "alice_peername@local.invalid"


def test_conversation_create_accepts_peer_address_for_local_peer(client) -> None:
    alice = register_user(client, "alice_peeraddr")
    bob = register_user(client, "bob_peeraddr")

    response = client.post(
        "/api/v1/conversations/dm",
        headers=auth_header(alice["tokens"]["access_token"]),
        json={"peer_address": "bob_peeraddr@local.invalid"},
    )
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["kind"] == "local"
    assert body["peer_username"] == "bob_peeraddr"
    assert body["peer_address"] == "bob_peeraddr@local.invalid"
