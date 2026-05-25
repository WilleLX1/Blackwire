import base64
from typing import Any

from nacl import encoding, public, signing


def _auth_header(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _new_device_material(label: str) -> dict[str, Any]:
    sign_sk = signing.SigningKey.generate()
    sign_pk_b64 = sign_sk.verify_key.encode(encoder=encoding.Base64Encoder).decode("utf-8")
    dh_sk = public.PrivateKey.generate()
    dh_pk_b64 = _b64(bytes(dh_sk.public_key))
    return {
        "label": label,
        "sign_pk_b64": sign_pk_b64,
        "dh_pk_b64": dh_pk_b64,
    }


def _register_user_v2(client, username: str) -> dict[str, Any]:
    response = client.post("/api/v2/auth/register", json={"username": username, "password": "Password123!"})
    assert response.status_code == 201, response.text
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


def test_v2_ws_webrtc_offer_disabled_returns_call_error(client) -> None:
    alice_reg = _register_user_v2(client, "alice_v2_webrtc_off")
    bob_reg = _register_user_v2(client, "bob_v2_webrtc_off")

    alice_device = _new_device_material("alice-v2-webrtc-device")
    bob_device = _new_device_material("bob-v2-webrtc-device")
    alice_tokens = _register_device_v2(client, alice_reg["tokens"]["bootstrap_token"], alice_device)["tokens"]
    bob_tokens = _register_device_v2(client, bob_reg["tokens"]["bootstrap_token"], bob_device)["tokens"]

    dm = client.post(
        "/api/v2/conversations/dm",
        headers=_auth_header(alice_tokens["access_token"]),
        json={"peer_username": "bob_v2_webrtc_off"},
    )
    assert dm.status_code == 200, dm.text
    conversation_id = dm.json()["id"]

    with client.websocket_connect("/api/v2/ws", headers=_auth_header(alice_tokens["access_token"])) as alice_ws:
        with client.websocket_connect("/api/v2/ws", headers=_auth_header(bob_tokens["access_token"])) as bob_ws:
            alice_ws.send_json({"type": "call.offer", "conversation_id": conversation_id})
            incoming = bob_ws.receive_json()
            assert incoming["type"] == "call.incoming"
            call_id = incoming["call_id"]
            assert alice_ws.receive_json()["type"] == "call.ringing"
            bob_ws.send_json({"type": "call.accept", "call_id": call_id})
            assert alice_ws.receive_json()["type"] == "call.accepted"
            assert bob_ws.receive_json()["type"] == "call.accepted"

            alice_ws.send_json(
                {
                    "type": "call.webrtc.offer",
                    "call_id": call_id,
                    "sdp": "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=Blackwire\r\nt=0 0\r\nm=audio 9 RTP/AVP 0\r\n",
                }
            )
            err = alice_ws.receive_json()
            assert err["type"] == "call.error"
            assert err["code"] == "webrtc_disabled"
