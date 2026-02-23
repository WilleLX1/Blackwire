import uuid
from datetime import UTC, datetime, timedelta

from sqlalchemy import DateTime, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class RatchetSkippedKey(Base):
    __tablename__ = "ratchet_skipped_keys"
    __table_args__ = (
        UniqueConstraint(
            "owner_device_uid",
            "peer_address",
            "peer_device_uid",
            "dh_pub_b64",
            "msg_n",
            name="uq_ratchet_skipped_owner_peer_dh_n",
        ),
    )

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    owner_device_uid: Mapped[str] = mapped_column(ForeignKey("devices.id", ondelete="CASCADE"), index=True)
    peer_address: Mapped[str] = mapped_column(String(320), index=True)
    peer_device_uid: Mapped[str] = mapped_column(String(64), index=True)
    dh_pub_b64: Mapped[str] = mapped_column(String(128))
    msg_n: Mapped[int] = mapped_column(Integer)
    mk_encrypted_b64: Mapped[str] = mapped_column(String(512))
    mk_nonce_b64: Mapped[str] = mapped_column(String(128))
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(UTC) + timedelta(days=7)
    )
    used_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
