import uuid
from datetime import UTC, datetime

from sqlalchemy import DateTime, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class DeviceOneTimePrekey(Base):
    __tablename__ = "device_one_time_prekeys"
    __table_args__ = (UniqueConstraint("device_id", "key_id", name="uq_device_one_time_prekey_device_key"),)

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    device_id: Mapped[str] = mapped_column(ForeignKey("devices.id", ondelete="CASCADE"), index=True)
    key_id: Mapped[int] = mapped_column(Integer)
    pub_x25519_b64: Mapped[str] = mapped_column(String(128))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
    consumed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), index=True, nullable=True)
    consumed_by_address: Mapped[str | None] = mapped_column(String(320), nullable=True)
