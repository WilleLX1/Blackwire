import pytest

from app.services.peer_address import parse_peer_address_with_policy


def test_policy_rejects_non_onion_authority_when_required() -> None:
    with pytest.raises(ValueError):
        parse_peer_address_with_policy("alice@local.invalid", require_onion_authority=True)


def test_policy_accepts_onion_authority_when_required() -> None:
    parsed = parse_peer_address_with_policy(
        "alice@abcdefghijklmnop.onion",
        require_onion_authority=True,
    )
    assert parsed.username == "alice"
    assert parsed.server_onion == "abcdefghijklmnop.onion"
