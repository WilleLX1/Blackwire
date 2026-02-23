from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field


class DeviceRegisterRequestV2(BaseModel):
    label: str = Field(min_length=1, max_length=100)
    pub_sign_key: str = Field(min_length=32, max_length=256)
    pub_dh_key: str = Field(min_length=32, max_length=256)


class DeviceOutV2(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    device_uid: str
    user_id: str
    label: str
    pub_sign_key: str
    pub_dh_key: str
    status: str
    created_at: datetime
    last_seen_at: datetime
    revoked_at: datetime | None


class UserDeviceLookupV2(BaseModel):
    username: str
    peer_address: str
    devices: list[DeviceOutV2]


class DeviceResolveListV2(BaseModel):
    peer_address: str
    devices: list[DeviceOutV2]

