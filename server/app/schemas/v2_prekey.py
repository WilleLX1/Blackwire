from datetime import datetime

from pydantic import BaseModel, Field


class SignedPrekeyUploadV2(BaseModel):
    key_id: int = Field(ge=1)
    pub_x25519_b64: str = Field(min_length=16, max_length=128)
    sig_by_device_sign_key_b64: str = Field(min_length=16, max_length=256)
    expires_at: datetime | None = None


class OneTimePrekeyUploadV2(BaseModel):
    key_id: int = Field(ge=1)
    pub_x25519_b64: str = Field(min_length=16, max_length=128)


class PrekeyUploadRequestV2(BaseModel):
    signed_prekey: SignedPrekeyUploadV2
    one_time_prekeys: list[OneTimePrekeyUploadV2] = Field(default_factory=list, max_length=512)


class SignedPrekeyOutV2(BaseModel):
    key_id: int
    pub_x25519_b64: str
    sig_by_device_sign_key_b64: str
    expires_at: datetime


class OneTimePrekeyOutV2(BaseModel):
    key_id: int
    pub_x25519_b64: str


class ResolvedPrekeyDeviceV2(BaseModel):
    device_uid: str
    pub_sign_key: str
    pub_dh_key: str
    supported_message_modes: list[str]
    signed_prekey: SignedPrekeyOutV2 | None = None
    one_time_prekey: OneTimePrekeyOutV2 | None = None
    opk_missing: bool = False


class ResolvePrekeysResponseV2(BaseModel):
    username: str
    peer_address: str
    devices: list[ResolvedPrekeyDeviceV2]


class PrekeyUploadResponseV2(BaseModel):
    uploaded_signed_prekey_key_id: int
    accepted_one_time_prekeys: int
