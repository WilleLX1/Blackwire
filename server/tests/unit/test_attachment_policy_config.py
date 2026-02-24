import pytest

from app.config import Settings


def test_attachment_policy_defaults_match_v02c_plan() -> None:
    settings = Settings(
        environment="test",
        database_url="sqlite+aiosqlite:///./test.db",
        auto_create_tables=False,
        attachment_inline_max_bytes=10 * 1024 * 1024,
        attachment_hard_ceiling_bytes=32 * 1024 * 1024,
    )
    assert settings.effective_attachment_inline_max_bytes() == 10 * 1024 * 1024
    assert settings.attachment_hard_ceiling_bytes == 32 * 1024 * 1024


def test_attachment_policy_rejects_ciphertext_above_hard_ceiling() -> None:
    with pytest.raises(ValueError):
        Settings(
            environment="test",
            database_url="sqlite+aiosqlite:///./test.db",
            auto_create_tables=False,
            max_ciphertext_bytes=33 * 1024 * 1024,
            attachment_hard_ceiling_bytes=32 * 1024 * 1024,
        )


def test_attachment_policy_rejects_client_body_smaller_than_ciphertext_limit() -> None:
    with pytest.raises(ValueError):
        Settings(
            environment="test",
            database_url="sqlite+aiosqlite:///./test.db",
            auto_create_tables=False,
            max_ciphertext_bytes=1024,
            max_client_message_body_bytes=512,
        )
