from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field

from app.schemas.user import UserOut


class RegisterRequestV2(BaseModel):
    username: str = Field(min_length=3, max_length=64, pattern=r"^[A-Za-z0-9_]{3,64}$")
    password: str = Field(min_length=8, max_length=128)


class LoginRequestV2(BaseModel):
    username: str = Field(min_length=3, max_length=64, pattern=r"^[A-Za-z0-9_]{3,64}$")
    password: str = Field(min_length=1, max_length=128)


class BindDeviceRequestV2(BaseModel):
    device_uid: str
    nonce: str = Field(min_length=8, max_length=128)
    timestamp_ms: int = Field(ge=0)
    proof_signature_b64: str = Field(min_length=16, max_length=1024)


class RefreshRequestV2(BaseModel):
    refresh_token: str


class LogoutRequestV2(BaseModel):
    refresh_token: str


class BootstrapTokenBundleV2(BaseModel):
    bootstrap_token: str
    token_type: str = "bootstrap"
    bootstrap_expires_in: int


class DeviceTokenBundleV2(BaseModel):
    access_token: str
    token_type: str = "bearer"
    access_expires_in: int
    refresh_token: str
    refresh_expires_in: int
    device_uid: str


class BootstrapAuthResponseV2(BaseModel):
    user: UserOut
    tokens: BootstrapTokenBundleV2


class DeviceAuthResponseV2(BaseModel):
    user: UserOut
    tokens: DeviceTokenBundleV2


class RefreshTokenRecordV2(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    user_id: str
    device_id: str | None
    token_kind: str
    issued_at: datetime
    expires_at: datetime
    revoked_at: datetime | None
    replaced_by: str | None
