from app.config import Settings
from app.services.server_authority import authority_matches, is_local_server_authority


def test_authority_matches_ignores_missing_port() -> None:
    assert authority_matches("localhost:8000", "localhost")
    assert authority_matches("127.0.0.1", "127.0.0.1:8000")


def test_is_local_server_authority_uses_configured_aliases() -> None:
    settings = Settings(
        environment="test",
        database_url="sqlite+aiosqlite:///./test.db",
        auto_create_tables=False,
        federation_server_onion="local.invalid",
        local_server_aliases="192.168.1.50:8000",
    )
    assert is_local_server_authority("192.168.1.50:8000", settings)
