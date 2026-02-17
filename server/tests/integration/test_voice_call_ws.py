import base64
import os

from tests.helpers import auth_header, register_device, register_user


def _ws_headers(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def _create_dm(client, token: str, peer_username: str) -> str:
    response = client.post(
        "/api/v1/conversations/dm",
        headers=auth_header(token),
        json={"peer_username": peer_username},
    )
    assert response.status_code == 200, response.text
    return response.json()["id"]


def test_offer_accept_audio_end(client) -> None:
    alice = register_user(client, "alice_voice_1")
    bob = register_user(client, "bob_voice_1")

    alice_token = alice["tokens"]["access_token"]
    bob_token = bob["tokens"]["access_token"]
    register_device(client, alice_token, "alice-voice-device")
    register_device(client, bob_token, "bob-voice-device")

    conversation_id = _create_dm(client, alice_token, "bob_voice_1")

    with client.websocket_connect("/api/v1/ws", headers=_ws_headers(alice_token)) as alice_ws:
        with client.websocket_connect("/api/v1/ws", headers=_ws_headers(bob_token)) as bob_ws:
            alice_ws.send_json({"type": "call.offer", "conversation_id": conversation_id})

            bob_incoming = bob_ws.receive_json()
            assert bob_incoming["type"] == "call.incoming"
            assert bob_incoming["conversation_id"] == conversation_id
            assert bob_incoming["from_user_address"] == "alice_voice_1@local.invalid"
            call_id = bob_incoming["call_id"]

            alice_ringing = alice_ws.receive_json()
            assert alice_ringing["type"] == "call.ringing"
            assert alice_ringing["call_id"] == call_id
            assert alice_ringing["peer_user_address"] == "bob_voice_1@local.invalid"

            bob_ws.send_json({"type": "call.accept", "call_id": call_id})

            accepted_a = alice_ws.receive_json()
            accepted_b = bob_ws.receive_json()
            assert accepted_a["type"] == "call.accepted"
            assert accepted_b["type"] == "call.accepted"
            assert accepted_a["call_id"] == call_id
            assert accepted_b["call_id"] == call_id

            frame = os.urandom(640)
            alice_ws.send_json(
                {
                    "type": "call.audio",
                    "call_id": call_id,
                    "sequence": 0,
                    "pcm_b64": base64.b64encode(frame).decode("ascii"),
                }
            )

            audio_event = bob_ws.receive_json()
            assert audio_event["type"] == "call.audio"
            assert audio_event["call_id"] == call_id
            assert audio_event["sequence"] == 0
            assert audio_event["from_user_id"] == alice["user"]["id"]
            assert audio_event["from_user_address"] == "alice_voice_1@local.invalid"

            bob_ws.send_json({"type": "call.end", "call_id": call_id, "reason": "ended"})
            ended_a = alice_ws.receive_json()
            ended_b = bob_ws.receive_json()
            assert ended_a["type"] == "call.ended"
            assert ended_b["type"] == "call.ended"
            assert ended_a["call_id"] == call_id


def test_busy_and_invalid_audio_errors(client) -> None:
    alice = register_user(client, "alice_voice_2")
    bob = register_user(client, "bob_voice_2")
    carol = register_user(client, "carol_voice_2")

    alice_token = alice["tokens"]["access_token"]
    bob_token = bob["tokens"]["access_token"]
    carol_token = carol["tokens"]["access_token"]
    register_device(client, alice_token, "alice-voice-device")
    register_device(client, bob_token, "bob-voice-device")
    register_device(client, carol_token, "carol-voice-device")

    ab_conversation_id = _create_dm(client, alice_token, "bob_voice_2")
    bc_conversation_id = _create_dm(client, bob_token, "carol_voice_2")

    with client.websocket_connect("/api/v1/ws", headers=_ws_headers(alice_token)) as alice_ws:
        with client.websocket_connect("/api/v1/ws", headers=_ws_headers(bob_token)) as bob_ws:
            with client.websocket_connect("/api/v1/ws", headers=_ws_headers(carol_token)) as carol_ws:
                bob_ws.send_json({"type": "call.offer", "conversation_id": bc_conversation_id})
                incoming = carol_ws.receive_json()
                assert incoming["type"] == "call.incoming"
                bob_ringing = bob_ws.receive_json()
                assert bob_ringing["type"] == "call.ringing"
                carol_ws.send_json({"type": "call.accept", "call_id": incoming["call_id"]})
                assert bob_ws.receive_json()["type"] == "call.accepted"
                assert carol_ws.receive_json()["type"] == "call.accepted"

                alice_ws.send_json({"type": "call.offer", "conversation_id": ab_conversation_id})
                busy = alice_ws.receive_json()
                assert busy["type"] == "call.busy"
                assert busy["reason"] == "peer_busy"

                alice_ws.send_json(
                    {
                        "type": "call.audio",
                        "call_id": "missing-call-id",
                        "sequence": 1,
                        "pcm_b64": base64.b64encode(os.urandom(640)).decode("ascii"),
                    }
                )
                audio_error = alice_ws.receive_json()
                assert audio_error["type"] == "call.error"
                assert audio_error["code"] == "call_not_found"


def test_ringing_timeout_emits_ended(client) -> None:
    alice = register_user(client, "alice_voice_3")
    bob = register_user(client, "bob_voice_3")

    alice_token = alice["tokens"]["access_token"]
    bob_token = bob["tokens"]["access_token"]
    register_device(client, alice_token, "alice-voice-device")
    register_device(client, bob_token, "bob-voice-device")

    conversation_id = _create_dm(client, alice_token, "bob_voice_3")

    with client.websocket_connect("/api/v1/ws", headers=_ws_headers(alice_token)) as alice_ws:
        with client.websocket_connect("/api/v1/ws", headers=_ws_headers(bob_token)) as bob_ws:
            alice_ws.send_json({"type": "call.offer", "conversation_id": conversation_id})
            incoming = bob_ws.receive_json()
            assert incoming["type"] == "call.incoming"
            assert alice_ws.receive_json()["type"] == "call.ringing"

            ended_for_alice = alice_ws.receive_json()
            ended_for_bob = bob_ws.receive_json()
            assert ended_for_alice["type"] == "call.ended"
            assert ended_for_bob["type"] == "call.ended"
            assert ended_for_alice["reason"] == "missed"
