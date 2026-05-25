import uuid
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import JSON, DateTime, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class GroupMembershipEvent(Base):
    __tablename__ = "group_membership_events"
    __table_args__ = (UniqueConstraint("group_uid", "event_seq", name="uq_group_membership_event_seq"),)

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    group_uid: Mapped[str] = mapped_column(String(64), index=True)
    event_seq: Mapped[int] = mapped_column(Integer, index=True)
    event_type: Mapped[str] = mapped_column(String(24), index=True)
    actor_address: Mapped[str] = mapped_column(String(320), default="")
    target_address: Mapped[str] = mapped_column(String(320), default="")
    payload_json: Mapped[dict[str, Any]] = mapped_column(JSON, default=dict)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
