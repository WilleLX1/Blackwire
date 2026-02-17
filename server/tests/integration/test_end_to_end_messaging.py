import base64

from tests.helpers import auth_header, register_device, register_user


def _ws_headers(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def test_realtime_and_offline_delivery(client) -> None:
    alice = register_user(client, "alice")
    bob = register_user(client, "bob")

    alice_token = alice["tokens"]["access_token"]
    bob_token = bob["tokens"]["access_token"]

    register_device(client, alice_token, "alice-device")
    bob_device = register_device(client, bob_token, "bob-device")

    dm = client.post(
        "/api/v1/conversations/dm",
        headers=auth_header(alice_token),
        json={"peer_username": "bob"},
    )
    assert dm.status_code == 200
    conversation_id = dm.json()["id"]

    first_payload = {
        "conversation_id": conversation_id,
        "envelope": {
            "version": 1,
            "alg": "libsodium-sealedbox-v1",
            "recipient_device_id": bob_device["id"],
            "ciphertext_b64": base64.b64encode(b"msg-online").decode("ascii"),
            "aad_b64": None,
            "client_message_id": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        },
    }

    ws_path = "/api/v1/ws"
    with client.websocket_connect(ws_path, headers=_ws_headers(bob_token)) as bob_ws:
        send_online = client.post(
            "/api/v1/messages/send",
            headers=auth_header(alice_token),
            json=first_payload,
        )
        assert send_online.status_code == 200
        online_event = bob_ws.receive_json()
        assert online_event["type"] == "message.new"
        assert online_event["message"]["sender_address"] == "alice@local.invalid"
        bob_ws.send_json({"type": "message.ack", "message_id": online_event["message"]["id"]})

    offline_payload = {
        "conversation_id": conversation_id,
        "envelope": {
            "version": 1,
            "alg": "libsodium-sealedbox-v1",
            "recipient_device_id": bob_device["id"],
            "ciphertext_b64": base64.b64encode(b"msg-offline").decode("ascii"),
            "aad_b64": None,
            "client_message_id": "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        },
    }
    send_offline = client.post(
        "/api/v1/messages/send",
        headers=auth_header(alice_token),
        json=offline_payload,
    )
    assert send_offline.status_code == 200

    with client.websocket_connect(ws_path, headers=_ws_headers(bob_token)) as bob_ws:
        offline_event = bob_ws.receive_json()
        assert offline_event["type"] == "message.new"
        assert offline_event["message"]["sender_address"] == "alice@local.invalid"
        assert (
            offline_event["message"]["envelope"]["client_message_id"]
            == "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
        )
        bob_ws.send_json({"type": "message.ack", "message_id": offline_event["message"]["id"]})
