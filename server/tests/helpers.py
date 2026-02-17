import base64
import secrets

from fastapi.testclient import TestClient


def auth_header(access_token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {access_token}"}


def random_b64(bytes_len: int = 32) -> str:
    return base64.b64encode(secrets.token_bytes(bytes_len)).decode("ascii")


def register_user(client: TestClient, username: str, password: str = "password123") -> dict:
    response = client.post(
        "/api/v1/auth/register",
        json={"username": username, "password": password},
    )
    assert response.status_code == 201, response.text
    return response.json()


def register_device(client: TestClient, access_token: str, label: str) -> dict:
    response = client.post(
        "/api/v1/devices/register",
        headers=auth_header(access_token),
        json={
            "label": label,
            "ik_ed25519_pub": random_b64(32),
            "enc_x25519_pub": random_b64(32),
        },
    )
    assert response.status_code == 200, response.text
    return response.json()
