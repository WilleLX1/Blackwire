from tests.helpers import register_user


def test_refresh_rotation_and_revocation(client) -> None:
    data = register_user(client, "alice")
    refresh_token = data["tokens"]["refresh_token"]

    refreshed = client.post("/api/v1/auth/refresh", json={"refresh_token": refresh_token})
    assert refreshed.status_code == 200, refreshed.text
    new_refresh_token = refreshed.json()["tokens"]["refresh_token"]

    old_refresh_reuse = client.post("/api/v1/auth/refresh", json={"refresh_token": refresh_token})
    assert old_refresh_reuse.status_code == 401

    logout = client.post("/api/v1/auth/logout", json={"refresh_token": new_refresh_token})
    assert logout.status_code == 204

    revoked_refresh = client.post("/api/v1/auth/refresh", json={"refresh_token": new_refresh_token})
    assert revoked_refresh.status_code == 401
