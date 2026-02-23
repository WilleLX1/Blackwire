from pydantic import BaseModel, Field

from app.schemas.v2_message import SignedEnvelopeV2


class FederationWellKnownOutV2(BaseModel):
    server_onion: str
    federation_version: str = "2"
    signing_public_key: str
    identity_binding_mode: str = "tor_v3_same_ed25519"


class FederationUserDevicesOutV2(BaseModel):
    username: str
    peer_address: str
    devices: list[dict]


class FederationMessageRelayEnvelopeV2(SignedEnvelopeV2):
    recipient_user_id: str = ""


class FederationMessageRelayRequestV2(BaseModel):
    relay_id: str
    conversation_id: str
    sender_address: str
    sender_device_uid: str = Field(min_length=1, max_length=64)
    sender_user_id: str | None = None
    client_message_id: str = Field(min_length=8, max_length=128)
    sent_at_ms: int = Field(ge=0)
    sender_prev_hash: str = Field(max_length=128, default="")
    sender_chain_hash: str = Field(min_length=16, max_length=128)
    envelopes: list[FederationMessageRelayEnvelopeV2] = Field(min_length=1, max_length=256)
