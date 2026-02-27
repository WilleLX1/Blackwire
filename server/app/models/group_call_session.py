import uuid
from datetime import UTC, datetime

from sqlalchemy import DateTime, ForeignKey, String
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class GroupCallSession(Base):
    __tablename__ = "group_call_sessions"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    call_id: Mapped[str] = mapped_column(String(36), unique=True, index=True)
    conversation_id: Mapped[str] = mapped_column(
        ForeignKey("conversations.id", ondelete="CASCADE"),
        index=True,
    )
    group_uid: Mapped[str] = mapped_column(String(64), index=True)
    initiator_address: Mapped[str] = mapped_column(String(320), index=True)
    state: Mapped[str] = mapped_column(String(16), default="ringing", index=True)
    started_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
    ended_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    ring_expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
