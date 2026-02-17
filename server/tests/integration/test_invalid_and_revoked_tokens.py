from tests.helpers import auth_header, register_user


def test_invalid_and_revoked_token_paths(client) -> None:
    invalid_me = client.get("/api/v1/me", headers=auth_header("not-a-real-token"))
    assert invalid_me.status_code == 401

    user = register_user(client, "alice")
    refresh_token = user["tokens"]["refresh_token"]

    revoked = client.post("/api/v1/auth/logout", json={"refresh_token": refresh_token})
    assert revoked.status_code == 204

    refresh_after_revoke = client.post(
        "/api/v1/auth/refresh",
        json={"refresh_token": refresh_token},
    )
    assert refresh_after_revoke.status_code == 401
