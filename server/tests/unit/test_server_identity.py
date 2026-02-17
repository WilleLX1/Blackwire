from pathlib import Path

import pytest

from app.config import get_settings, reset_settings_cache
from app.services.server_identity import get_server_onion, get_server_onion_source, initialize_server_identity


def test_tor_enabled_requires_hidden_service_onion(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("BLACKWIRE_TOR_ENABLED", "true")
    monkeypatch.setenv("BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
    monkeypatch.setenv("BLACKWIRE_TOR_HS_HOSTNAME_FILE", "Z:/missing/hidden_service/hostname")
    monkeypatch.setenv("BLACKWIRE_TOR_HS_HOSTNAME_WAIT_SECONDS", "0")

    reset_settings_cache()
    with pytest.raises(RuntimeError):
        initialize_server_identity(get_settings())
    reset_settings_cache()


def test_tor_disabled_allows_fallback_identity(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("BLACKWIRE_TOR_ENABLED", "false")
    monkeypatch.setenv("BLACKWIRE_FEDERATION_SERVER_ONION", "local.invalid")

    reset_settings_cache()
    initialize_server_identity(get_settings())
    assert get_server_onion() == "local.invalid"
    assert get_server_onion_source() == "configured_fallback"
    reset_settings_cache()


def test_tor_enabled_uses_hidden_service_onion(monkeypatch: pytest.MonkeyPatch) -> None:
    hostname_file = Path(__file__).resolve().parent / "data" / "valid_hs_hostname.txt"
    monkeypatch.setenv("BLACKWIRE_TOR_ENABLED", "true")
    monkeypatch.setenv("BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
    monkeypatch.setenv("BLACKWIRE_TOR_HS_HOSTNAME_FILE", str(hostname_file))
    monkeypatch.setenv("BLACKWIRE_TOR_HS_HOSTNAME_WAIT_SECONDS", "0")

    reset_settings_cache()
    initialize_server_identity(get_settings())
    assert get_server_onion() == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.onion"
    assert get_server_onion_source() == "tor_hidden_service"
    reset_settings_cache()
