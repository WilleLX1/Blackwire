from pydantic import BaseModel, Field, field_validator

_VALID_STATUSES = {"active", "inactive", "offline", "dnd"}


class PresenceSetRequest(BaseModel):
    status: str = Field(min_length=3, max_length=16)

    @field_validator("status")
    @classmethod
    def validate_status(cls, value: str) -> str:
        normalized = value.strip().lower()
        if normalized not in _VALID_STATUSES:
            raise ValueError("status must be one of: active, inactive, offline, dnd")
        return normalized


class PresenceSetResponse(BaseModel):
    status: str


class PresenceResolveRequest(BaseModel):
    peer_addresses: list[str] = Field(default_factory=list, max_length=256)

    @field_validator("peer_addresses")
    @classmethod
    def normalize_addresses(cls, value: list[str]) -> list[str]:
        normalized: list[str] = []
        seen: set[str] = set()
        for raw in value:
            item = raw.strip().lower()
            if not item:
                continue
            if item in seen:
                continue
            seen.add(item)
            normalized.append(item)
        return normalized


class PresencePeerOut(BaseModel):
    peer_address: str
    status: str


class PresenceResolveResponse(BaseModel):
    self_status: str
    peers: list[PresencePeerOut]
