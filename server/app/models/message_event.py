import uuid
from datetime import UTC, datetime

from sqlalchemy import BigInteger, DateTime, ForeignKey, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class MessageEvent(Base):
    __tablename__ = "message_events"
    __table_args__ = (
        UniqueConstraint("sender_device_uid", "client_message_id", name="uq_message_event_sender_client_message"),
    )

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    conversation_id: Mapped[str] = mapped_column(
        ForeignKey("conversations.id", ondelete="CASCADE"), index=True
    )
    sender_user_id: Mapped[str | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL"), index=True, nullable=True
    )
    sender_address: Mapped[str] = mapped_column(String(320), index=True, default="")
    sender_device_uid: Mapped[str] = mapped_column(String(36), index=True)
    sender_device_pubkey: Mapped[str] = mapped_column(String(256))
    client_message_id: Mapped[str] = mapped_column(String(64), index=True)
    sent_at_ms: Mapped[int] = mapped_column(BigInteger, index=True)
    encryption_mode: Mapped[str] = mapped_column(String(32), index=True, default="sealedbox_v0_2a")
    sender_prev_hash: Mapped[str] = mapped_column(String(128), default="")
    sender_chain_hash: Mapped[str] = mapped_column(String(128), index=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
