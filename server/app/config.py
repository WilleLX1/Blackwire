from functools import lru_cache
from typing import Literal

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="BLACKWIRE_", case_sensitive=False)

    app_name: str = "Blackwire"
    environment: Literal["dev", "test", "prod"] = "dev"
    api_prefix: str = "/api/v1"

    database_url: str = "sqlite+aiosqlite:///./blackwire.db"
    auto_create_tables: bool = False

    jwt_secret_key: str = "change-this-secret-minimum-32-bytes"
    jwt_algorithm: str = "HS256"
    access_token_minutes: int = 15
    refresh_token_days: int = 30
    jwt_private_key_pem: str = ""
    jwt_public_key_pem: str = ""
    v2_bootstrap_token_seconds: int = 120
    v2_access_token_minutes: int = 10
    v2_refresh_token_days: int = 30
    v2_bind_request_skew_seconds: int = 300

    message_ttl_days: int = 7
    queue_cleanup_interval_seconds: int = 60

    max_ciphertext_bytes: int = 65536
    max_aad_bytes: int = 4096
    voice_call_ring_timeout_seconds: int = 30
    voice_audio_max_chunk_bytes: int = 4096
    voice_audio_min_interval_ms: int = 8

    tor_enabled: bool = False
    tor_socks5_url: str = "socks5h://127.0.0.1:9050"
    tor_hs_hostname_file: str = "/var/lib/tor/hidden_service/hostname"
    tor_hs_ed25519_secret_key_file: str = "/var/lib/tor/hidden_service/hs_ed25519_secret_key"
    tor_hs_hostname_wait_seconds: int = 45

    federation_server_onion: str = "local.invalid"
    federation_signing_private_key_b64: str = ""
    federation_request_skew_seconds: int = 60
    federation_nonce_ttl_seconds: int = 300
    federation_outbox_poll_interval_seconds: int = 5

    rate_limit_per_minute: int = 120
    use_redis_rate_limit: bool = False
    redis_url: str | None = None

    allow_origins: list[str] = Field(default_factory=lambda: [])


@lru_cache
def get_settings() -> Settings:
    return Settings()


def reset_settings_cache() -> None:
    get_settings.cache_clear()
