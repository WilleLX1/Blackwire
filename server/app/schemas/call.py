import base64
import binascii

from pydantic import BaseModel, Field, field_validator


class CallOfferRequest(BaseModel):
    conversation_id: str


class CallAcceptRequest(BaseModel):
    call_id: str


class CallRejectRequest(BaseModel):
    call_id: str
    reason: str | None = Field(default=None, max_length=64)


class CallEndRequest(BaseModel):
    call_id: str
    reason: str | None = Field(default=None, max_length=64)


class CallAudioRequest(BaseModel):
    call_id: str
    sequence: int = Field(ge=0)
    pcm_b64: str

    @field_validator("pcm_b64")
    @classmethod
    def validate_pcm_b64(cls, value: str) -> str:
        try:
            base64.b64decode(value, validate=True)
        except binascii.Error as exc:
            raise ValueError("pcm_b64 must be valid base64") from exc
        return value


class CallWebRtcOfferRequest(BaseModel):
    call_id: str
    sdp: str = Field(min_length=8, max_length=131072)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=2, ge=2, le=8)
    target_user_address: str = Field(default="", max_length=320)
    source_user_address: str = Field(default="", max_length=320)


class CallWebRtcAnswerRequest(BaseModel):
    call_id: str
    sdp: str = Field(min_length=8, max_length=131072)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=2, ge=2, le=8)
    target_user_address: str = Field(default="", max_length=320)
    source_user_address: str = Field(default="", max_length=320)


class CallWebRtcIceRequest(BaseModel):
    call_id: str
    candidate: str = Field(min_length=1, max_length=16384)
    sdp_mid: str | None = Field(default=None, max_length=128)
    sdp_mline_index: int | None = Field(default=None, ge=0)
    call_schema_version: int = Field(default=1, ge=1)
    call_mode: str = Field(default="webrtc", max_length=32)
    max_participants: int = Field(default=2, ge=2, le=8)
    target_user_address: str = Field(default="", max_length=320)
    source_user_address: str = Field(default="", max_length=320)
