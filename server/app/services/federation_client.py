from urllib.parse import quote

import httpx
import orjson
from pydantic import ValidationError

from app.config import get_settings
from app.schemas.device import UserDeviceLookup
from app.schemas.v2_device import UserDeviceLookupV2
from app.schemas.v2_federation import FederationGroupSnapshotOutV2
from app.schemas.v2_prekey import ResolvePrekeysResponseV2
from app.services.federation_security import federation_security_service
from app.services.peer_address import is_onion_authority


class FederationClientError(Exception):
    def __init__(self, status_code: int, detail: str) -> None:
        super().__init__(detail)
        self.status_code = status_code
        self.detail = detail


class FederationClient:
    def __init__(self) -> None:
        self.settings = get_settings()

    async def _build_http_client(self) -> httpx.AsyncClient:
        if self.settings.tor_enabled:
            return httpx.AsyncClient(timeout=10.0, proxy=self.settings.tor_socks5_url)
        return httpx.AsyncClient(timeout=10.0)

    def _build_base_url(self, peer: str) -> str:
        """Return http:// for .onion hosts (routed through Tor), https:// for clearnet."""
        if is_onion_authority(peer):
            return f"http://{peer}"
        return f"https://{peer}"

    async def get_remote_user_device(self, peer_onion: str, username: str) -> UserDeviceLookup:
        peer = peer_onion.strip().lower()
        normalized_username = username.strip().lower()
        if not is_onion_authority(peer):
            raise FederationClientError(400, "Invalid remote server onion authority")
        if not normalized_username:
            raise FederationClientError(400, "Invalid remote username")
        base = self._build_base_url(peer)
        async with await self._build_http_client() as client:
            try:
                response = await client.get(
                    f"{base}{self.settings.api_prefix}/federation/users/{quote(normalized_username, safe='')}/device"
                )
            except httpx.HTTPError as exc:
                raise FederationClientError(
                    502,
                    f"Remote device lookup failed for {normalized_username}@{peer}",
                ) from exc

        if response.status_code >= 400:
            raise FederationClientError(response.status_code, response.text or "Remote device lookup failed")
        return UserDeviceLookup.model_validate(response.json())

    async def get_remote_user_devices_v2(self, peer_onion: str, username: str) -> UserDeviceLookupV2:
        peer = peer_onion.strip().lower()
        normalized_username = username.strip().lower()
        if not is_onion_authority(peer):
            raise FederationClientError(400, "Invalid remote server onion authority")
        if not normalized_username:
            raise FederationClientError(400, "Invalid remote username")

        async with await self._build_http_client() as client:
            try:
                base = self._build_base_url(peer)
                response = await client.get(
                    f"{base}/api/v2/federation/users/{quote(normalized_username, safe='')}/devices"
                )
            except httpx.HTTPError as exc:
                raise FederationClientError(
                    502,
                    f"Remote device lookup failed for {normalized_username}@{peer}",
                ) from exc

        if response.status_code >= 400:
            raise FederationClientError(response.status_code, response.text or "Remote device lookup failed")
        payload = response.json()
        try:
            return UserDeviceLookupV2.model_validate(payload)
        except ValidationError:
            fallback_payload = {
                "username": payload.get("username", normalized_username),
                "peer_address": payload.get("peer_address", ""),
                "devices": payload.get("devices", []),
                "attachment_inline_max_bytes": 0,
                "max_ciphertext_bytes": 0,
                "attachment_policy_source": "fallback_local",
            }
            return UserDeviceLookupV2.model_validate(fallback_payload)

    async def get_remote_user_prekeys_v2(self, peer_onion: str, username: str) -> ResolvePrekeysResponseV2:
        peer = peer_onion.strip().lower()
        normalized_username = username.strip().lower()
        if not is_onion_authority(peer):
            raise FederationClientError(400, "Invalid remote server onion authority")
        if not normalized_username:
            raise FederationClientError(400, "Invalid remote username")

        async with await self._build_http_client() as client:
            try:
                base = self._build_base_url(peer)
                response = await client.get(
                    f"{base}/api/v2/federation/users/{quote(normalized_username, safe='')}/prekeys"
                )
            except httpx.HTTPError as exc:
                raise FederationClientError(
                    502,
                    f"Remote prekey lookup failed for {normalized_username}@{peer}",
                ) from exc

        if response.status_code >= 400:
            raise FederationClientError(response.status_code, response.text or "Remote prekey lookup failed")
        return ResolvePrekeysResponseV2.model_validate(response.json())

    async def get_remote_group_snapshot_v2(self, peer_onion: str, group_uid: str) -> FederationGroupSnapshotOutV2:
        peer = peer_onion.strip().lower()
        normalized_group_uid = group_uid.strip().lower()
        if not is_onion_authority(peer):
            raise FederationClientError(400, "Invalid remote server onion authority")
        if not normalized_group_uid:
            raise FederationClientError(400, "Invalid group uid")

        async with await self._build_http_client() as client:
            try:
                base = self._build_base_url(peer)
                response = await client.get(
                    f"{base}/api/v2/federation/groups/{quote(normalized_group_uid, safe='')}/snapshot"
                )
            except httpx.HTTPError as exc:
                raise FederationClientError(
                    502, f"Remote group snapshot lookup failed for {normalized_group_uid}"
                ) from exc

        if response.status_code >= 400:
            raise FederationClientError(response.status_code, response.text or "Remote group snapshot failed")
        return FederationGroupSnapshotOutV2.model_validate(response.json())

    async def post_signed(
        self,
        peer_onion: str,
        endpoint_path: str,
        payload_json: dict,
    ) -> None:
        peer = peer_onion.strip().lower()
        if not is_onion_authority(peer):
            raise FederationClientError(400, "Invalid remote server onion authority")
        body_bytes = orjson.dumps(payload_json)
        headers = federation_security_service.sign_headers("POST", endpoint_path, body_bytes)
        headers["Content-Type"] = "application/json"
        base = self._build_base_url(peer)

        async with await self._build_http_client() as client:
            try:
                response = await client.post(
                    f"{base}{endpoint_path}",
                    content=body_bytes,
                    headers=headers,
                )
            except httpx.HTTPError as exc:
                raise FederationClientError(502, f"Federation request failed to {peer}{endpoint_path}") from exc

        if response.status_code >= 400:
            raise FederationClientError(response.status_code, response.text or "Federation request rejected")


federation_client = FederationClient()
