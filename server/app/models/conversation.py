import uuid
from datetime import UTC, datetime

from sqlalchemy import DateTime, ForeignKey, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class Conversation(Base):
    __tablename__ = "conversations"
    __table_args__ = (
        UniqueConstraint("kind", "user_a_id", "user_b_id", name="uq_conversation_local_pair"),
        UniqueConstraint("kind", "local_user_id", "peer_address", name="uq_conversation_remote_peer"),
    )

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    kind: Mapped[str] = mapped_column(String(16), default="local", index=True)

    user_a_id: Mapped[str | None] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True, nullable=True)
    user_b_id: Mapped[str | None] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True, nullable=True)

    local_user_id: Mapped[str | None] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), index=True, nullable=True
    )
    peer_username: Mapped[str] = mapped_column(String(64), default="")
    peer_server_onion: Mapped[str] = mapped_column(String(255), default="")
    peer_address: Mapped[str] = mapped_column(String(320), default="", index=True)
    conversation_type: Mapped[str] = mapped_column(String(16), default="direct", index=True)
    group_uid: Mapped[str | None] = mapped_column(String(64), nullable=True, unique=True, index=True)
    group_name: Mapped[str] = mapped_column(String(128), default="")
    origin_server_onion: Mapped[str] = mapped_column(String(255), default="")
    owner_address: Mapped[str] = mapped_column(String(320), default="")

    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=lambda: datetime.now(UTC))
