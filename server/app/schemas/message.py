import base64
import binascii
from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, field_validator


class CipherEnvelope(BaseModel):
    version: int = Field(ge=1)
    alg: str
    recipient_device_id: str
    ciphertext_b64: str
    aad_b64: str | None = None
    client_message_id: str

    @field_validator("alg")
    @classmethod
    def validate_alg(cls, value: str) -> str:
        if value != "libsodium-sealedbox-v1":
            raise ValueError("Unsupported encryption algorithm")
        return value

    @field_validator("ciphertext_b64")
    @classmethod
    def validate_ciphertext_b64(cls, value: str) -> str:
        try:
            base64.b64decode(value, validate=True)
        except binascii.Error as exc:
            raise ValueError("ciphertext_b64 must be valid base64") from exc
        return value

    @field_validator("aad_b64")
    @classmethod
    def validate_aad_b64(cls, value: str | None) -> str | None:
        if value is None:
            return value
        try:
            base64.b64decode(value, validate=True)
        except binascii.Error as exc:
            raise ValueError("aad_b64 must be valid base64") from exc
        return value


class MessageSendRequest(BaseModel):
    conversation_id: str
    envelope: CipherEnvelope


class MessageOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    conversation_id: str
    sender_user_id: str | None = None
    sender_address: str = ""
    sender_device_id: str
    client_message_id: str
    envelope_json: dict
    created_at: datetime


class MessageSendResponse(BaseModel):
    duplicate: bool
    message: MessageOut


class MessageAckRequest(BaseModel):
    message_id: str
