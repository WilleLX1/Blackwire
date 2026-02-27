"""group call core

Revision ID: 20260224_0008
Revises: 20260224_0007
Create Date: 2026-02-24 10:55:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260224_0008"
down_revision: str | None = "20260224_0007"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "group_call_sessions",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("call_id", sa.String(length=36), nullable=False, unique=True),
        sa.Column(
            "conversation_id",
            sa.String(length=36),
            sa.ForeignKey("conversations.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("group_uid", sa.String(length=64), nullable=False),
        sa.Column("initiator_address", sa.String(length=320), nullable=False),
        sa.Column("state", sa.String(length=16), nullable=False),
        sa.Column("started_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("ended_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("ring_expires_at", sa.DateTime(timezone=True), nullable=False),
    )
    op.create_index("ix_group_call_sessions_call_id", "group_call_sessions", ["call_id"], unique=True)
    op.create_index(
        "ix_group_call_sessions_conversation_id",
        "group_call_sessions",
        ["conversation_id"],
        unique=False,
    )
    op.create_index("ix_group_call_sessions_group_uid", "group_call_sessions", ["group_uid"], unique=False)
    op.create_index("ix_group_call_sessions_state", "group_call_sessions", ["state"], unique=False)
    op.create_index(
        "ix_group_call_sessions_ring_expires_at",
        "group_call_sessions",
        ["ring_expires_at"],
        unique=False,
    )

    op.create_table(
        "group_call_participants",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column(
            "call_id",
            sa.String(length=36),
            sa.ForeignKey("group_call_sessions.call_id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("member_address", sa.String(length=320), nullable=False),
        sa.Column(
            "local_user_id",
            sa.String(length=36),
            sa.ForeignKey("users.id", ondelete="SET NULL"),
            nullable=True,
        ),
        sa.Column("state", sa.String(length=24), nullable=False),
        sa.Column("invited_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("joined_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("left_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("last_signal_at", sa.DateTime(timezone=True), nullable=True),
        sa.UniqueConstraint("call_id", "member_address", name="uq_group_call_participant_address"),
    )
    op.create_index("ix_group_call_participants_call_id", "group_call_participants", ["call_id"], unique=False)
    op.create_index(
        "ix_group_call_participants_member_address",
        "group_call_participants",
        ["member_address"],
        unique=False,
    )
    op.create_index(
        "ix_group_call_participants_local_user_id",
        "group_call_participants",
        ["local_user_id"],
        unique=False,
    )
    op.create_index("ix_group_call_participants_state", "group_call_participants", ["state"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_group_call_participants_state", table_name="group_call_participants")
    op.drop_index("ix_group_call_participants_local_user_id", table_name="group_call_participants")
    op.drop_index("ix_group_call_participants_member_address", table_name="group_call_participants")
    op.drop_index("ix_group_call_participants_call_id", table_name="group_call_participants")
    op.drop_table("group_call_participants")

    op.drop_index("ix_group_call_sessions_ring_expires_at", table_name="group_call_sessions")
    op.drop_index("ix_group_call_sessions_state", table_name="group_call_sessions")
    op.drop_index("ix_group_call_sessions_group_uid", table_name="group_call_sessions")
    op.drop_index("ix_group_call_sessions_conversation_id", table_name="group_call_sessions")
    op.drop_index("ix_group_call_sessions_call_id", table_name="group_call_sessions")
    op.drop_table("group_call_sessions")
