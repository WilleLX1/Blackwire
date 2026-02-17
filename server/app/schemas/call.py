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
