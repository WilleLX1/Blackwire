"""conversation read cursors

Revision ID: 20260301_0009
Revises: 20260224_0008
Create Date: 2026-03-01 14:10:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260301_0009"
down_revision: str | None = "20260224_0008"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "conversation_read_cursors",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column(
            "conversation_id",
            sa.String(length=36),
            sa.ForeignKey("conversations.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column(
            "user_id",
            sa.String(length=36),
            sa.ForeignKey("users.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("user_address", sa.String(length=320), nullable=False),
        sa.Column("last_read_message_id", sa.String(length=36), nullable=False),
        sa.Column("last_read_sent_at_ms", sa.BigInteger(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint("conversation_id", "user_id", name="uq_conversation_read_cursor_user"),
    )
    op.create_index(
        "ix_conversation_read_cursors_conversation_id",
        "conversation_read_cursors",
        ["conversation_id"],
        unique=False,
    )
    op.create_index("ix_conversation_read_cursors_user_id", "conversation_read_cursors", ["user_id"], unique=False)
    op.create_index(
        "ix_conversation_read_cursors_user_address",
        "conversation_read_cursors",
        ["user_address"],
        unique=False,
    )
    op.create_index(
        "ix_conversation_read_cursors_last_read_sent_at_ms",
        "conversation_read_cursors",
        ["last_read_sent_at_ms"],
        unique=False,
    )
    op.create_index(
        "ix_conversation_read_cursors_updated_at",
        "conversation_read_cursors",
        ["updated_at"],
        unique=False,
    )
    op.create_index(
        "ix_conversation_read_cursors_conversation_updated_at",
        "conversation_read_cursors",
        ["conversation_id", "updated_at"],
        unique=False,
    )


def downgrade() -> None:
    op.drop_index("ix_conversation_read_cursors_conversation_updated_at", table_name="conversation_read_cursors")
    op.drop_index("ix_conversation_read_cursors_updated_at", table_name="conversation_read_cursors")
    op.drop_index("ix_conversation_read_cursors_last_read_sent_at_ms", table_name="conversation_read_cursors")
    op.drop_index("ix_conversation_read_cursors_user_address", table_name="conversation_read_cursors")
    op.drop_index("ix_conversation_read_cursors_user_id", table_name="conversation_read_cursors")
    op.drop_index("ix_conversation_read_cursors_conversation_id", table_name="conversation_read_cursors")
    op.drop_table("conversation_read_cursors")
