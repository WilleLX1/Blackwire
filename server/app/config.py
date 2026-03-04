from functools import lru_cache
from typing import Literal

from pydantic import Field
from pydantic import model_validator
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

    max_ciphertext_bytes: int = 20971520
    max_aad_bytes: int = 4096
    attachment_inline_max_bytes: int = 10485760
    attachment_hard_ceiling_bytes: int = 33554432
    max_client_message_body_bytes: int = 31457280
    max_federation_body_bytes: int = 31457280
    message_bytes_per_minute_per_user: int = 52428800
    federation_bytes_per_minute_per_peer: int = 104857600
    pending_queue_max_copies_per_device: int = 500
    pending_queue_max_bytes_per_device: int = 67108864
    enable_ratchet_v2b1: bool = False
    ratchet_require_for_local: bool = False
    ratchet_require_for_federation: bool = False
    enable_webrtc_v2b2: bool = False
    webrtc_ice_servers_json: str = ""
    enable_legacy_call_audio_ws: bool = True
    voice_call_ring_timeout_seconds: int = 30
    voice_audio_max_chunk_bytes: int = 4096
    voice_audio_min_interval_ms: int = 8
    enable_group_dm_v2c: bool = True
    enable_group_call_v2c: bool = True
    enable_typing_v03b: bool = True
    enable_read_cursor_v03b: bool = True
    typing_indicator_ttl_seconds: int = 6
    typing_event_rate_per_minute: int = 240
    read_cursor_write_rate_per_minute: int = 240
    group_max_members: int = 32
    group_call_max_participants: int = 8
    group_invite_rate_per_minute: int = 120
    group_create_rate_per_hour: int = 30
    group_call_start_rate_per_minute: int = 30
    group_call_ring_ttl_seconds: int = 45

    tor_enabled: bool = False
    tor_socks5_url: str = "socks5h://127.0.0.1:9050"
    tor_hs_hostname_file: str = "/var/lib/tor/hidden_service/hostname"
    tor_hs_ed25519_secret_key_file: str = "/var/lib/tor/hidden_service/hs_ed25519_secret_key"
    tor_hs_hostname_wait_seconds: int = 45

    federation_server_onion: str = "local.invalid"
    local_server_aliases: str = ""
    federation_signing_private_key_b64: str = ""
    federation_request_skew_seconds: int = 60
    federation_nonce_ttl_seconds: int = 300
    federation_outbox_poll_interval_seconds: int = 5

    tls_enabled: bool = True
    tls_cert_file: str = "/app/certs/server.crt"
    tls_key_file: str = "/app/certs/server.key"

    rate_limit_per_minute: int = 120
    use_redis_rate_limit: bool = False
    redis_url: str | None = None

    allow_origins: list[str] = Field(default_factory=lambda: [])

    def effective_attachment_inline_max_bytes(self) -> int:
        return min(self.attachment_inline_max_bytes, self.attachment_hard_ceiling_bytes)

    def effective_max_ciphertext_bytes(self) -> int:
        return min(self.max_ciphertext_bytes, self.attachment_hard_ceiling_bytes)

    @model_validator(mode="after")
    def validate_attachment_limits(self) -> "Settings":
        if self.attachment_hard_ceiling_bytes <= 0:
            raise ValueError("attachment_hard_ceiling_bytes must be greater than 0")
        if self.attachment_inline_max_bytes <= 0:
            raise ValueError("attachment_inline_max_bytes must be greater than 0")
        if self.max_ciphertext_bytes <= 0:
            raise ValueError("max_ciphertext_bytes must be greater than 0")
        if self.max_client_message_body_bytes <= 0:
            raise ValueError("max_client_message_body_bytes must be greater than 0")
        if self.max_federation_body_bytes <= 0:
            raise ValueError("max_federation_body_bytes must be greater than 0")
        if self.message_bytes_per_minute_per_user <= 0:
            raise ValueError("message_bytes_per_minute_per_user must be greater than 0")
        if self.federation_bytes_per_minute_per_peer <= 0:
            raise ValueError("federation_bytes_per_minute_per_peer must be greater than 0")
        if self.pending_queue_max_copies_per_device <= 0:
            raise ValueError("pending_queue_max_copies_per_device must be greater than 0")
        if self.pending_queue_max_bytes_per_device <= 0:
            raise ValueError("pending_queue_max_bytes_per_device must be greater than 0")
        if self.group_max_members < 2:
            raise ValueError("group_max_members must be at least 2")
        if self.group_call_max_participants < 2:
            raise ValueError("group_call_max_participants must be at least 2")
        if self.group_call_max_participants > self.group_max_members:
            raise ValueError("group_call_max_participants cannot exceed group_max_members")
        if self.group_invite_rate_per_minute <= 0:
            raise ValueError("group_invite_rate_per_minute must be greater than 0")
        if self.group_create_rate_per_hour <= 0:
            raise ValueError("group_create_rate_per_hour must be greater than 0")
        if self.group_call_start_rate_per_minute <= 0:
            raise ValueError("group_call_start_rate_per_minute must be greater than 0")
        if self.group_call_ring_ttl_seconds <= 0:
            raise ValueError("group_call_ring_ttl_seconds must be greater than 0")
        if self.typing_indicator_ttl_seconds <= 0:
            raise ValueError("typing_indicator_ttl_seconds must be greater than 0")
        if self.typing_event_rate_per_minute <= 0:
            raise ValueError("typing_event_rate_per_minute must be greater than 0")
        if self.read_cursor_write_rate_per_minute <= 0:
            raise ValueError("read_cursor_write_rate_per_minute must be greater than 0")
        if self.attachment_inline_max_bytes > self.attachment_hard_ceiling_bytes:
            raise ValueError("attachment_inline_max_bytes cannot exceed attachment_hard_ceiling_bytes")
        if self.max_ciphertext_bytes > self.attachment_hard_ceiling_bytes:
            raise ValueError("max_ciphertext_bytes cannot exceed attachment_hard_ceiling_bytes")
        if self.max_client_message_body_bytes < self.effective_max_ciphertext_bytes():
            raise ValueError("max_client_message_body_bytes cannot be smaller than max_ciphertext_bytes")
        if self.max_federation_body_bytes < self.effective_max_ciphertext_bytes():
            raise ValueError("max_federation_body_bytes cannot be smaller than max_ciphertext_bytes")
        return self


@lru_cache
def get_settings() -> Settings:
    return Settings()


def reset_settings_cache() -> None:
    get_settings.cache_clear()
