from tests.helpers import auth_header


def test_auth_and_me_include_canonical_identity_fields(client) -> None:
    register = client.post(
        "/api/v1/auth/register",
        json={"username": "alice_identity", "password": "password123"},
    )
    assert register.status_code == 201, register.text
    register_body = register.json()
    assert register_body["user"]["user_address"] == "alice_identity@local.invalid"
    assert register_body["user"]["home_server_onion"] == "local.invalid"

    login = client.post(
        "/api/v1/auth/login",
        json={"username": "alice_identity", "password": "password123"},
    )
    assert login.status_code == 200, login.text
    login_body = login.json()
    assert login_body["user"]["user_address"] == "alice_identity@local.invalid"
    assert login_body["user"]["home_server_onion"] == "local.invalid"

    refresh = client.post(
        "/api/v1/auth/refresh",
        json={"refresh_token": login_body["tokens"]["refresh_token"]},
    )
    assert refresh.status_code == 200, refresh.text
    refresh_body = refresh.json()
    assert refresh_body["user"]["user_address"] == "alice_identity@local.invalid"
    assert refresh_body["user"]["home_server_onion"] == "local.invalid"

    me = client.get("/api/v1/me", headers=auth_header(refresh_body["tokens"]["access_token"]))
    assert me.status_code == 200, me.text
    me_body = me.json()
    assert me_body["user_address"] == "alice_identity@local.invalid"
    assert me_body["home_server_onion"] == "local.invalid"
