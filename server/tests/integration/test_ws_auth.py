from tests.helpers import register_user


def test_websocket_accepts_query_access_token(client) -> None:
    alice = register_user(client, "alice_ws_query_auth")
    access_token = alice["tokens"]["access_token"]

    with client.websocket_connect(f"/api/v1/ws?access_token={access_token}") as ws:
        ws.send_json({"type": "unsupported"})
        response = ws.receive_json()
        assert response["type"] == "error"
        assert response["code"] == "unsupported_event"
