from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field


class DeviceRegisterRequest(BaseModel):
    label: str = Field(min_length=1, max_length=100)
    ik_ed25519_pub: str = Field(min_length=32, max_length=256)
    enc_x25519_pub: str = Field(min_length=32, max_length=256)


class DeviceOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    user_id: str
    label: str
    ik_ed25519_pub: str
    enc_x25519_pub: str
    created_at: datetime


class UserDeviceLookup(BaseModel):
    username: str
    peer_address: str = ""
    device: DeviceOut
