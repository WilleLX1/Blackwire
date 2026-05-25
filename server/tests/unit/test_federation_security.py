from app.config import get_settings
from app.services.federation_security import canonical_request_string, federation_security_service
from app.services.server_identity import initialize_server_identity


def test_canonical_request_string_is_stable() -> None:
    canonical = canonical_request_string(
        method="POST",
        path="/api/v1/federation/messages/relay",
        body_bytes=b'{"hello":"world"}',
        timestamp="1700000000",
        nonce="nonce-1",
        sender_onion="peer.onion",
    )
    expected = "\n".join(
        [
            "POST",
            "/api/v1/federation/messages/relay",
            "93a23971a914e5eacbf0a8d25154cda309c3c1c72fbb9914d47c60f3cb681588",
            "1700000000",
            "nonce-1",
            "peer.onion",
        ]
    )
    assert canonical.decode("utf-8") == expected


def test_sign_headers_include_required_fields() -> None:
    initialize_server_identity(get_settings())
    headers = federation_security_service.sign_headers(
        "POST",
        "/api/v1/federation/messages/relay",
        b'{"relay_id":"r1"}',
    )
    assert "X-BW-Fed-Server" in headers
    assert "X-BW-Fed-Timestamp" in headers
    assert "X-BW-Fed-Nonce" in headers
    assert "X-BW-Fed-Signature" in headers
