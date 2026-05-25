import uuid
from datetime import UTC, datetime, timedelta

from sqlalchemy import DateTime, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class DeviceSignedPrekey(Base):
    __tablename__ = "device_signed_prekeys"
    __table_args__ = (UniqueConstraint("device_id", "key_id", name="uq_device_signed_prekey_device_key"),)

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    device_id: Mapped[str] = mapped_column(ForeignKey("devices.id", ondelete="CASCADE"), index=True)
    key_id: Mapped[int] = mapped_column(Integer)
    pub_x25519_b64: Mapped[str] = mapped_column(String(128))
    sig_by_device_sign_key_b64: Mapped[str] = mapped_column(String(256))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=lambda: datetime.now(UTC) + timedelta(days=30)
    )
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
