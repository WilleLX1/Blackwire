import base64
import binascii
from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, field_validator


class RatchetHeaderV2(BaseModel):
    v: str = Field(default="dr_v1", min_length=3, max_length=16)
    dh_pub: str = Field(min_length=16, max_length=256)
    n: int = Field(ge=0)
    pn: int = Field(ge=0)


class RatchetInitV2(BaseModel):
    scheme: str = Field(default="x3dh_v1", min_length=4, max_length=32)
    sender_ephemeral_pub: str = Field(min_length=16, max_length=256)
    signed_prekey_id: int | None = Field(default=None, ge=1)
    one_time_prekey_id: int | None = Field(default=None, ge=1)
    opk_missing: bool = False


class SignedEnvelopeV2(BaseModel):
    recipient_user_address: str
    recipient_device_uid: str
    ciphertext_b64: str
    aad_b64: str | None = None
    signature_b64: str
    sender_device_pubkey: str
    ratchet_header: RatchetHeaderV2 | None = None
    ratchet_init: RatchetInitV2 | None = None

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


class MessageSendRequestV2(BaseModel):
    conversation_id: str
    encryption_mode: str = Field(default="sealedbox_v0_2a", max_length=32)
    client_message_id: str = Field(min_length=8, max_length=128)
    sent_at_ms: int = Field(ge=0)
    sender_prev_hash: str = Field(max_length=128, default="")
    sender_chain_hash: str = Field(min_length=16, max_length=128)
    envelopes: list[SignedEnvelopeV2] = Field(min_length=1, max_length=256)


class MessageEventOutV2(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    conversation_id: str
    sender_user_id: str | None = None
    sender_address: str
    sender_device_uid: str
    sender_device_pubkey: str
    encryption_mode: str = "sealedbox_v0_2a"
    client_message_id: str
    sent_at_ms: int
    sender_prev_hash: str
    sender_chain_hash: str
    created_at: datetime


class MessageDeviceCopyOutV2(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    copy_id: str
    message: MessageEventOutV2
    recipient_device_uid: str
    envelope_json: dict
    status: str
    created_at: datetime


class MessageSendResponseV2(BaseModel):
    duplicate: bool
    message: MessageEventOutV2


class MessageAckRequestV2(BaseModel):
    copy_id: str
