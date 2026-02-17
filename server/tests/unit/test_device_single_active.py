import asyncio

from sqlalchemy import select

from app.db import get_session_factory
from app.models.device import ActiveDevice
from tests.helpers import auth_header, register_device, register_user


def test_single_active_device_enforced(client) -> None:
    data = register_user(client, "alice")
    token = data["tokens"]["access_token"]

    first = register_device(client, token, "laptop")
    second = register_device(client, token, "phone")

    lookup = client.get(
        "/api/v1/users/alice/device",
        headers=auth_header(token),
    )
    assert lookup.status_code == 200
    assert lookup.json()["device"]["id"] == second["id"]
    assert lookup.json()["device"]["id"] != first["id"]

    async def active_device_count() -> int:
        session_factory = get_session_factory()
        async with session_factory() as session:
            rows = list((await session.execute(select(ActiveDevice))).scalars().all())
            return len(rows)

    assert asyncio.run(active_device_count()) == 1
