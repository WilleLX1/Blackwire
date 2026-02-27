from pydantic import BaseModel, Field

from app.schemas.v2_message import SignedEnvelopeV2
from app.schemas.v2_prekey import ResolvePrekeysResponseV2


class FederationWellKnownOutV2(BaseModel):
    server_onion: str
    federation_version: str = "2"
    signing_public_key: str
    identity_binding_mode: str = "tor_v3_same_ed25519"
    attachment_inline_max_bytes: int = Field(default=10485760, ge=1)
    max_ciphertext_bytes: int = Field(default=20971520, ge=1)
    attachment_hard_ceiling_bytes: int = Field(default=33554432, ge=1)
    supported_message_modes: list[str] = Field(default_factory=lambda: ["sealedbox_v0_2a"])
    supported_call_modes: list[str] = Field(default_factory=lambda: ["ws_pcm_v0_2a"])


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
    encryption_mode: str = Field(default="sealedbox_v0_2a", max_length=32)
    client_message_id: str = Field(min_length=8, max_length=128)
    sent_at_ms: int = Field(ge=0)
    sender_prev_hash: str = Field(max_length=128, default="")
    sender_chain_hash: str = Field(min_length=16, max_length=128)
    envelopes: list[FederationMessageRelayEnvelopeV2] = Field(min_length=1, max_length=256)
    group_uid: str | None = Field(default=None, max_length=64)


class FederationUserPrekeysOutV2(ResolvePrekeysResponseV2):
    pass


class FederationGroupEventRequestV2(BaseModel):
    relay_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    event_seq: int = Field(ge=1)
    event_type: str = Field(min_length=3, max_length=24)
    actor_address: str = Field(min_length=3, max_length=320)
    target_address: str = Field(default="", max_length=320)
    payload: dict = Field(default_factory=dict)
    created_at: str = Field(min_length=8, max_length=128)


class FederationGroupSnapshotOutV2(BaseModel):
    group_uid: str
    conversation_id: str
    group_name: str
    origin_server_onion: str
    owner_address: str
    latest_event_seq: int
    members: list[dict]


class FederationGroupInviteAcceptRequestV2(BaseModel):
    relay_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    conversation_id: str = Field(min_length=8, max_length=64)
    actor_address: str = Field(min_length=3, max_length=320)


class FederationCallWebRtcOfferRequestV2(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str
    sdp: str = Field(min_length=8, max_length=131072)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=2, ge=2, le=8)
    target_user_address: str = Field(default="", max_length=320)
    source_user_address: str = Field(default="", max_length=320)


class FederationCallWebRtcAnswerRequestV2(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str
    sdp: str = Field(min_length=8, max_length=131072)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=2, ge=2, le=8)
    target_user_address: str = Field(default="", max_length=320)
    source_user_address: str = Field(default="", max_length=320)


class FederationCallWebRtcIceRequestV2(BaseModel):
    relay_id: str
    call_id: str
    from_user_address: str
    to_user_address: str
    candidate: str = Field(min_length=1, max_length=16384)
    sdp_mid: str | None = Field(default=None, max_length=128)
    sdp_mline_index: int | None = Field(default=None, ge=0)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=2, ge=2, le=8)
    target_user_address: str = Field(default="", max_length=320)
    source_user_address: str = Field(default="", max_length=320)


class FederationGroupCallOfferRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    conversation_id: str = Field(min_length=8, max_length=64)
    from_user_address: str = Field(min_length=3, max_length=320)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=8, ge=2, le=8)
    ring_expires_at: str = Field(min_length=8, max_length=128)


class FederationGroupCallJoinRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    member_address: str = Field(min_length=3, max_length=320)


class FederationGroupCallLeaveRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    member_address: str = Field(min_length=3, max_length=320)
    reason: str = Field(default="left", max_length=64)


class FederationGroupCallEndRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    reason: str = Field(default="ended", max_length=64)


class FederationGroupCallWebRtcOfferRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    source_user_address: str = Field(min_length=3, max_length=320)
    target_user_address: str = Field(min_length=3, max_length=320)
    sdp: str = Field(min_length=8, max_length=131072)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=8, ge=2, le=8)


class FederationGroupCallWebRtcAnswerRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    source_user_address: str = Field(min_length=3, max_length=320)
    target_user_address: str = Field(min_length=3, max_length=320)
    sdp: str = Field(min_length=8, max_length=131072)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=8, ge=2, le=8)


class FederationGroupCallWebRtcIceRequestV2(BaseModel):
    relay_id: str
    call_id: str
    group_uid: str = Field(min_length=8, max_length=64)
    source_user_address: str = Field(min_length=3, max_length=320)
    target_user_address: str = Field(min_length=3, max_length=320)
    candidate: str = Field(min_length=1, max_length=16384)
    sdp_mid: str | None = Field(default=None, max_length=128)
    sdp_mline_index: int | None = Field(default=None, ge=0)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=8, ge=2, le=8)
