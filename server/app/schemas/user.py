from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.services.server_identity import get_server_onion, server_address_for_username


class UserOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    username: str
    created_at: datetime
    user_address: str = ""
    home_server_onion: str = ""

    @classmethod
    def from_user(cls, user: object) -> "UserOut":
        username = str(getattr(user, "username", "")).strip().lower()
        return cls(
            id=str(getattr(user, "id")),
            username=username,
            created_at=getattr(user, "created_at"),
            user_address=server_address_for_username(username),
            home_server_onion=get_server_onion(),
        )
