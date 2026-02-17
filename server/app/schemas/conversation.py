from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, model_validator


class CreateDMConversationRequest(BaseModel):
    peer_username: str | None = Field(
        default=None,
        min_length=3,
        max_length=64,
        pattern=r"^[A-Za-z0-9_]{3,64}$",
    )
    peer_address: str | None = Field(default=None, min_length=3, max_length=320)

    @model_validator(mode="after")
    def validate_peer_target(self) -> "CreateDMConversationRequest":
        if (self.peer_username is None or not self.peer_username.strip()) and (
            self.peer_address is None or not self.peer_address.strip()
        ):
            raise ValueError("Either peer_username or peer_address is required")
        return self


class ConversationOut(BaseModel):
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
