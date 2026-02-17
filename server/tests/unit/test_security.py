import pytest

from app.security.password import hash_password, verify_password
from app.security.tokens import TokenError, create_access_token, decode_token


def test_password_hash_and_verify() -> None:
    raw = "top-secret-password"
    hashed = hash_password(raw)
    assert hashed != raw
    assert verify_password(raw, hashed)
    assert not verify_password("wrong", hashed)


def test_access_token_roundtrip() -> None:
    token, _ = create_access_token(user_id="user-1", username="alice")
    payload = decode_token(token, expected_type="access")
    assert payload["sub"] == "user-1"
    assert payload["username"] == "alice"


def test_access_token_wrong_type() -> None:
    token, _ = create_access_token(user_id="user-2", username="bob")
    with pytest.raises(TokenError):
        decode_token(token, expected_type="refresh")
