from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field

from app.schemas.user import UserOut


class RegisterRequest(BaseModel):
    username: str = Field(min_length=3, max_length=64, pattern=r"^[A-Za-z0-9_]{3,64}$")
    password: str = Field(min_length=8, max_length=128)


class LoginRequest(BaseModel):
    username: str = Field(min_length=3, max_length=64, pattern=r"^[A-Za-z0-9_]{3,64}$")
    password: str = Field(min_length=1, max_length=128)


class RefreshRequest(BaseModel):
    refresh_token: str


class LogoutRequest(BaseModel):
    refresh_token: str


class TokenBundle(BaseModel):
    access_token: str
    token_type: str = "bearer"
    access_expires_in: int
    refresh_token: str
    refresh_expires_in: int


class AuthResponse(BaseModel):
    user: UserOut
    tokens: TokenBundle


class RefreshTokenRecord(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    user_id: str
    issued_at: datetime
    expires_at: datetime
    revoked_at: datetime | None
    replaced_by: str | None
