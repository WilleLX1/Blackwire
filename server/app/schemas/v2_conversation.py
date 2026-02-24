from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

from app.schemas.v2_device import DeviceOutV2
from app.schemas.v2_prekey import ResolvedPrekeyDeviceV2


class CreateDMConversationRequestV2(BaseModel):
    peer_username: str | None = Field(
        default=None,
        min_length=3,
        max_length=64,
        pattern=r"^[A-Za-z0-9_]{3,64}$",
    )
    peer_address: str | None = Field(default=None, min_length=3, max_length=320)

    @model_validator(mode="after")
    def validate_peer_target(self) -> "CreateDMConversationRequestV2":
        if (self.peer_username is None or not self.peer_username.strip()) and (
            self.peer_address is None or not self.peer_address.strip()
        ):
            raise ValueError("Either peer_username or peer_address is required")
        return self


class CreateGroupConversationRequestV2(BaseModel):
    name: str = Field(min_length=1, max_length=128)
    member_addresses: list[str] = Field(default_factory=list, max_length=64)


class GroupInviteRequestV2(BaseModel):
    member_addresses: list[str] = Field(min_length=1, max_length=64)


class GroupRenameRequestV2(BaseModel):
    name: str = Field(min_length=1, max_length=128)


class ConversationOutV2(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    kind: str = "local"
    user_a_id: str = ""
    user_b_id: str = ""
    local_user_id: str = ""
    created_at: datetime
    peer_username: str = ""
    peer_server_onion: str = ""
    peer_address: str = ""
    conversation_type: Literal["direct", "group"] = "direct"
    group_uid: str | None = None
    group_name: str = ""
    member_count: int = 0
    membership_state: Literal["none", "invited", "active", "left", "removed"] = "none"
    can_manage_members: bool = False
    origin_server_onion: str = ""
    owner_address: str = ""


class ConversationMemberOutV2(BaseModel):
    id: str
    member_user_id: str | None = None
    member_address: str
    member_server_onion: str
    role: Literal["owner", "member"] = "member"
    status: Literal["invited", "active", "left", "removed"] = "invited"
    invited_by_address: str = ""
    invited_at: datetime
    joined_at: datetime | None = None
    left_at: datetime | None = None
    updated_at: datetime


class ConversationRecipientDeviceOutV2(BaseModel):
    member_address: str
    member_status: Literal["active", "invited", "left", "removed"] = "active"
    device: DeviceOutV2
    prekey: ResolvedPrekeyDeviceV2 | None = None


class ConversationRecipientsOutV2(BaseModel):
    conversation_id: str
    conversation_type: Literal["direct", "group"] = "direct"
    recipients: list[ConversationRecipientDeviceOutV2]
