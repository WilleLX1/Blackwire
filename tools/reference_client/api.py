from typing import Any

import requests


class ApiError(ValueError):
    pass


def _request(method: str, url: str, **kwargs: Any) -> dict[str, Any]:
    response = requests.request(method=method, url=url, timeout=15, **kwargs)
    if response.status_code >= 400:
        raise ApiError(f"{response.status_code} {response.text}")
    if response.status_code == 204:
        return {}
    return response.json()


def auth_headers(access_token: str | None) -> dict[str, str]:
    if not access_token:
        raise ApiError("Access token missing. Run login first.")
    return {"Authorization": f"Bearer {access_token}"}


def register(base_url: str, username: str, password: str) -> dict[str, Any]:
    return _request(
        "POST",
        f"{base_url}/api/v1/auth/register",
        json={"username": username, "password": password},
    )


def login(base_url: str, username: str, password: str) -> dict[str, Any]:
    return _request(
        "POST",
        f"{base_url}/api/v1/auth/login",
        json={"username": username, "password": password},
    )


def register_device(
    base_url: str,
    access_token: str,
    label: str,
    ik_ed25519_pub: str,
    enc_x25519_pub: str,
) -> dict[str, Any]:
    return _request(
        "POST",
        f"{base_url}/api/v1/devices/register",
        headers=auth_headers(access_token),
        json={
            "label": label,
            "ik_ed25519_pub": ik_ed25519_pub,
            "enc_x25519_pub": enc_x25519_pub,
        },
    )


def get_user_device(base_url: str, access_token: str, username: str) -> dict[str, Any]:
    return _request(
        "GET",
        f"{base_url}/api/v1/users/{username}/device",
        headers=auth_headers(access_token),
    )


def create_dm(base_url: str, access_token: str, peer_username: str) -> dict[str, Any]:
    return _request(
        "POST",
        f"{base_url}/api/v1/conversations/dm",
        headers=auth_headers(access_token),
        json={"peer_username": peer_username},
    )


def send_message(base_url: str, access_token: str, payload: dict[str, Any]) -> dict[str, Any]:
    return _request(
        "POST",
        f"{base_url}/api/v1/messages/send",
        headers=auth_headers(access_token),
        json=payload,
    )
