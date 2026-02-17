import asyncio
import base64
from datetime import UTC, datetime, timedelta

from sqlalchemy import select

from app.db import get_session_factory
from app.models.delivery_queue import DeliveryQueue
from app.services.message_service import message_service
from tests.helpers import auth_header, register_device, register_user


def test_idempotent_send_and_queue_expiry(client) -> None:
    alice = register_user(client, "alice")
    bob = register_user(client, "bob")

    alice_token = alice["tokens"]["access_token"]
    bob_token = bob["tokens"]["access_token"]

    register_device(client, alice_token, "alice-device")
    bob_device = register_device(client, bob_token, "bob-device")

    dm_response = client.post(
        "/api/v1/conversations/dm",
        headers=auth_header(alice_token),
        json={"peer_username": "bob"},
    )
    assert dm_response.status_code == 200, dm_response.text
    conversation_id = dm_response.json()["id"]

    ciphertext = base64.b64encode(b"hello").decode("ascii")
    payload = {
        "conversation_id": conversation_id,
        "envelope": {
            "version": 1,
            "alg": "libsodium-sealedbox-v1",
            "recipient_device_id": bob_device["id"],
            "ciphertext_b64": ciphertext,
            "aad_b64": None,
            "client_message_id": "11111111-1111-4111-8111-111111111111",
        },
    }

    first_send = client.post("/api/v1/messages/send", headers=auth_header(alice_token), json=payload)
    assert first_send.status_code == 200
    assert first_send.json()["duplicate"] is False
    message_id = first_send.json()["message"]["id"]

    second_send = client.post("/api/v1/messages/send", headers=auth_header(alice_token), json=payload)
    assert second_send.status_code == 200
    assert second_send.json()["duplicate"] is True
    assert second_send.json()["message"]["id"] == message_id

    async def expire_and_verify() -> str:
        session_factory = get_session_factory()
        async with session_factory() as session:
            queue_item = (
                await session.execute(
                    select(DeliveryQueue).where(DeliveryQueue.message_id == message_id)
                )
            ).scalar_one()
            queue_item.expires_at = datetime.now(UTC) - timedelta(seconds=1)
            await session.commit()

            await message_service.expire_old(session)

            refreshed = (
                await session.execute(
                    select(DeliveryQueue).where(DeliveryQueue.message_id == message_id)
                )
            ).scalar_one()
            return refreshed.status

    assert asyncio.run(expire_and_verify()) == "expired"
