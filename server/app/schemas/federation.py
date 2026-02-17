import base64
import binascii

from pydantic import BaseModel, Field, field_validator

from app.schemas.message import CipherEnvelope


class FederationWellKnownOut(BaseModel):
    server_onion: str
    federation_version: str = "1"
    signing_public_key: str


class FederationUserDeviceOut(BaseModel):
    username: str
    peer_address: str
    ik_ed25519_pub: str
    enc_x25519_pub: str
    device_id: str


class FederationMessageRelayRequest(BaseModel):
    relay_id: str
    sender_address: str
    sender_device_id: str = Field(min_length=1, max_length=128)
    recipient_address: str
    recipient_device_id: str
    client_message_id: str
    envelope: CipherEnvelope


class FederationCallOfferRequest(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str


class FederationCallAcceptRequest(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str


class FederationCallRejectRequest(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str
    reason: str = "declined"


class FederationCallEndRequest(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str
    reason: str = "ended"


class FederationCallAudioRequest(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str
    sequence: int = Field(ge=0)
    pcm_b64: str = Field(max_length=8192)

    @field_validator("pcm_b64")
    @classmethod
    def validate_pcm_b64(cls, value: str) -> str:
        try:
            base64.b64decode(value, validate=True)
        except binascii.Error as exc:
            raise ValueError("pcm_b64 must be valid base64") from exc
        return value
