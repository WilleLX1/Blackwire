import pytest
from starlette.websockets import WebSocketDisconnect

from tests.helpers import auth_header, register_user


def test_websocket_rejects_query_access_token_and_accepts_bearer(client) -> None:
    alice = register_user(client, "alice_ws_query_auth")
    access_token = alice["tokens"]["access_token"]

    with pytest.raises(WebSocketDisconnect) as exc_info:
        with client.websocket_connect(f"/api/v1/ws?access_token={access_token}"):
            pass
    assert exc_info.value.code == 1008

    with client.websocket_connect("/api/v1/ws", headers=auth_header(access_token)) as ws:
        ws.send_json({"type": "unsupported"})
        response = ws.receive_json()
        assert response["type"] == "error"
        assert response["code"] == "unsupported_event"
