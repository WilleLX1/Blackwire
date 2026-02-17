import base64
import hashlib
import re
import time
from datetime import UTC, datetime, timedelta
from uuid import uuid4

import httpx
from fastapi import HTTPException, Request, status
from nacl import encoding, signing
from nacl import exceptions as nacl_exceptions
from sqlalchemy import delete, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.federation_nonce_replay import FederationNonceReplay
from app.models.federation_peer import FederationPeer
from app.schemas.federation import FederationWellKnownOut
from app.services.server_identity import (
    get_federation_signing_key,
    get_server_onion,
)

_ONION_AUTHORITY_PATTERN = re.compile(r"^[a-z2-7]{16,56}\.onion$")
_NONCE_PATTERN = re.compile(r"^[a-zA-Z0-9._:-]{8,128}$")


def canonical_request_string(
    method: str,
    path: str,
    body_bytes: bytes,
    timestamp: str,
    nonce: str,
    sender_onion: str,
) -> bytes:
    body_hash = hashlib.sha256(body_bytes).hexdigest()
    canonical = "\n".join(
        [
            method.upper(),
            path,
            body_hash,
            timestamp,
            nonce,
            sender_onion.strip().lower(),
        ]
    )
    return canonical.encode("utf-8")


class FederationSecurityService:
    def __init__(self) -> None:
        self.settings = get_settings()

    async def _build_http_client(self) -> httpx.AsyncClient:
        if self.settings.tor_enabled:
            return httpx.AsyncClient(timeout=10.0, proxy=self.settings.tor_socks5_url)
        return httpx.AsyncClient(timeout=10.0)

    async def fetch_well_known(self, peer_onion: str) -> FederationWellKnownOut:
        normalized = peer_onion.strip().lower()
        if not self._is_valid_peer_onion(normalized):
            raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="Invalid peer onion")

        async with await self._build_http_client() as client:
            try:
                response = await client.get(f"http://{normalized}{self.settings.api_prefix}/federation/well-known")
                response.raise_for_status()
            except httpx.HTTPError as exc:
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail=f"Failed peer discovery for {normalized}",
                ) from exc

        payload = FederationWellKnownOut.model_validate(response.json())
        if payload.server_onion.strip().lower() != normalized:
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail="Peer well-known server identity mismatch",
            )
        return payload

    async def get_or_onboard_peer(self, session: AsyncSession, peer_onion: str) -> FederationPeer:
        normalized = peer_onion.strip().lower()
        stmt = select(FederationPeer).where(FederationPeer.onion == normalized)
        peer = (await session.execute(stmt)).scalar_one_or_none()

        if peer is None:
            discovered = await self.fetch_well_known(normalized)
            peer = FederationPeer(
                onion=normalized,
                signing_public_key=discovered.signing_public_key,
                status="active",
            )
            session.add(peer)
            await session.flush()
            return peer

        if peer.status == "key_conflict":
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Peer key conflict")

        peer.last_seen_at = datetime.now(UTC)
        await session.flush()
        return peer

    @staticmethod
    def _is_valid_peer_onion(peer_onion: str) -> bool:
        return bool(_ONION_AUTHORITY_PATTERN.fullmatch(peer_onion))

    @staticmethod
    def _is_valid_nonce(nonce: str) -> bool:
        return bool(_NONCE_PATTERN.fullmatch(nonce))

    def sign_headers(self, method: str, path: str, body_bytes: bytes) -> dict[str, str]:
        timestamp = str(int(time.time()))
        nonce = str(uuid4())
        sender_onion = get_server_onion()
        message = canonical_request_string(
            method=method,
            path=path,
            body_bytes=body_bytes,
            timestamp=timestamp,
            nonce=nonce,
            sender_onion=sender_onion,
        )
        signature = get_federation_signing_key().sign(message).signature
        return {
            "X-BW-Fed-Server": sender_onion,
            "X-BW-Fed-Timestamp": timestamp,
            "X-BW-Fed-Nonce": nonce,
            "X-BW-Fed-Signature": base64.b64encode(signature).decode("utf-8"),
        }

    async def verify_incoming(self, session: AsyncSession, request: Request, body_bytes: bytes) -> str:
        headers = request.headers
        peer_onion = headers.get("X-BW-Fed-Server", "").strip().lower()
        timestamp = headers.get("X-BW-Fed-Timestamp", "").strip()
        nonce = headers.get("X-BW-Fed-Nonce", "").strip()
        signature_b64 = headers.get("X-BW-Fed-Signature", "").strip()

        if not peer_onion or not timestamp or not nonce or not signature_b64:
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing federation auth headers")
        if not self._is_valid_peer_onion(peer_onion):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid federation peer onion")
        if peer_onion == get_server_onion():
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Federation self-sender is not allowed",
            )
        if not self._is_valid_nonce(nonce):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid federation nonce")

        try:
            ts_value = int(timestamp)
        except ValueError as exc:
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Invalid federation timestamp",
            ) from exc

        now = int(time.time())
        if abs(now - ts_value) > self.settings.federation_request_skew_seconds:
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Federation timestamp skew exceeded")

        peer = await self.get_or_onboard_peer(session, peer_onion)

        canonical = canonical_request_string(
            method=request.method,
            path=request.url.path,
            body_bytes=body_bytes,
            timestamp=timestamp,
            nonce=nonce,
            sender_onion=peer_onion,
        )
        try:
            signature = base64.b64decode(signature_b64.encode("utf-8"), validate=True)
            verify_key = signing.VerifyKey(peer.signing_public_key.encode("utf-8"), encoder=encoding.Base64Encoder)
            verify_key.verify(canonical, signature)
        except (ValueError, nacl_exceptions.BadSignatureError) as exc:
            await session.rollback()
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Invalid federation signature",
            ) from exc

        now_dt = datetime.now(UTC)
        await session.execute(delete(FederationNonceReplay).where(FederationNonceReplay.expires_at <= now_dt))
        nonce_row = FederationNonceReplay(
            peer_onion=peer_onion,
            nonce=nonce,
            expires_at=now_dt + timedelta(seconds=self.settings.federation_nonce_ttl_seconds),
        )
        session.add(nonce_row)
        try:
            await session.commit()
        except IntegrityError as exc:
            await session.rollback()
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Federation nonce replay detected",
            ) from exc

        return peer_onion


federation_security_service = FederationSecurityService()
