import asyncio
import os
from collections.abc import Generator
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.config import reset_settings_cache
from app.db import reset_engine
from app.security.tokens_v2 import reset_v2_token_cache


@pytest.fixture()
def client(tmp_path: Path) -> Generator[TestClient, None, None]:
    db_path = tmp_path / "test.db"
    os.environ["BLACKWIRE_ENVIRONMENT"] = "test"
    os.environ["BLACKWIRE_DATABASE_URL"] = f"sqlite+aiosqlite:///{db_path.as_posix()}"
    os.environ["BLACKWIRE_AUTO_CREATE_TABLES"] = "true"
    os.environ["BLACKWIRE_JWT_SECRET_KEY"] = "test-secret-key-with-at-least-32-bytes"
    os.environ["BLACKWIRE_RATE_LIMIT_PER_MINUTE"] = "10000"
    os.environ["BLACKWIRE_VOICE_CALL_RING_TIMEOUT_SECONDS"] = "1"
    os.environ["BLACKWIRE_TOR_ENABLED"] = "false"
    os.environ["BLACKWIRE_FEDERATION_SERVER_ONION"] = "local.invalid"
    os.environ["BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64"] = ""

    reset_settings_cache()
    reset_v2_token_cache()
    asyncio.run(reset_engine())

    from app.main import create_app

    app = create_app()

    with TestClient(app) as test_client:
        yield test_client

    asyncio.run(reset_engine())
    reset_settings_cache()
    reset_v2_token_cache()
