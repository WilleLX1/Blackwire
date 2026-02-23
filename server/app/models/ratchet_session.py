import uuid
from datetime import UTC, datetime

from sqlalchemy import DateTime, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class RatchetSession(Base):
    __tablename__ = "ratchet_sessions"
    __table_args__ = (
        UniqueConstraint(
            "owner_device_uid",
            "peer_address",
            "peer_device_uid",
            name="uq_ratchet_session_owner_peer",
        ),
    )

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    owner_user_id: Mapped[str] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    owner_device_uid: Mapped[str] = mapped_column(ForeignKey("devices.id", ondelete="CASCADE"), index=True)
    peer_address: Mapped[str] = mapped_column(String(320), index=True)
    peer_device_uid: Mapped[str] = mapped_column(String(64), index=True)
    session_version: Mapped[str] = mapped_column(String(16), default="dr_v1")
    state_blob_encrypted_b64: Mapped[str] = mapped_column(String(65536), default="")
    state_nonce_b64: Mapped[str] = mapped_column(String(128), default="")
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
    last_send_chain_n: Mapped[int] = mapped_column(Integer, default=0)
    last_recv_chain_n: Mapped[int] = mapped_column(Integer, default=0)
    last_root_key_hash: Mapped[str] = mapped_column(String(128), default="")
