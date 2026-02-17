import re
from dataclasses import dataclass

_ONION_AUTHORITY_PATTERN = re.compile(r"^[a-z2-7]{16,56}\.onion$")
_USERNAME_PATTERN = re.compile(r"^[a-z0-9_]{3,64}$")


@dataclass(frozen=True)
class ParsedPeerAddress:
    username: str
    server_onion: str

    @property
    def canonical(self) -> str:
        return f"{self.username}@{self.server_onion}"


def parse_peer_address(value: str) -> ParsedPeerAddress:
    raw = value.strip().lower()
    if not raw:
        raise ValueError("peer_address is required")

    if raw.count("@") != 1:
        raise ValueError("peer_address must be in username@onion format")

    username, onion = raw.split("@", 1)
    username = username.strip()
    onion = onion.strip()
    if not username or not onion:
        raise ValueError("peer_address must be in username@onion format")
    if not _USERNAME_PATTERN.fullmatch(username):
        raise ValueError("username must be 3-64 chars and contain only lowercase letters, digits, or underscore")
    if len(onion) < 5:
        raise ValueError("server onion is invalid")
    return ParsedPeerAddress(username=username, server_onion=onion)


def is_onion_authority(value: str) -> bool:
    return bool(_ONION_AUTHORITY_PATTERN.fullmatch(value.strip().lower()))


def parse_peer_address_with_policy(value: str, require_onion_authority: bool) -> ParsedPeerAddress:
    parsed = parse_peer_address(value)
    if require_onion_authority and not is_onion_authority(parsed.server_onion):
        raise ValueError("peer_address server must be a valid .onion authority")
    return parsed
