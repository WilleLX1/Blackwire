def test_federation_well_known_exposes_identity_and_key(client) -> None:
    response = client.get("/api/v1/federation/well-known")
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["server_onion"] == "local.invalid"
    assert body["federation_version"] == "1"
    assert isinstance(body["signing_public_key"], str)
    assert len(body["signing_public_key"]) > 10
