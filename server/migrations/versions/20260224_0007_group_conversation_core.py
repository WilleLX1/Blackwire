"""group conversation core

Revision ID: 20260224_0007
Revises: 20260223_0006
Create Date: 2026-02-24 10:30:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260224_0007"
down_revision: str | None = "20260223_0006"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.add_column(
        "conversations",
        sa.Column("conversation_type", sa.String(length=16), nullable=False, server_default="direct"),
    )
    op.add_column(
        "conversations",
        sa.Column("group_uid", sa.String(length=64), nullable=True),
    )
    op.add_column(
        "conversations",
        sa.Column("group_name", sa.String(length=128), nullable=False, server_default=""),
    )
    op.add_column(
        "conversations",
        sa.Column("origin_server_onion", sa.String(length=255), nullable=False, server_default=""),
    )
    op.add_column(
        "conversations",
        sa.Column("owner_address", sa.String(length=320), nullable=False, server_default=""),
    )
    op.create_index("ix_conversations_conversation_type", "conversations", ["conversation_type"], unique=False)
    op.create_index("ix_conversations_group_uid", "conversations", ["group_uid"], unique=True)

    op.create_table(
        "conversation_members",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column(
            "conversation_id",
            sa.String(length=36),
            sa.ForeignKey("conversations.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column(
            "member_user_id",
            sa.String(length=36),
            sa.ForeignKey("users.id", ondelete="SET NULL"),
            nullable=True,
        ),
        sa.Column("member_address", sa.String(length=320), nullable=False),
        sa.Column("member_server_onion", sa.String(length=255), nullable=False),
        sa.Column("role", sa.String(length=16), nullable=False),
        sa.Column("status", sa.String(length=16), nullable=False),
        sa.Column("invited_by_address", sa.String(length=320), nullable=False),
        sa.Column("invited_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("joined_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("left_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint("conversation_id", "member_address", name="uq_conversation_member_address"),
    )
    op.create_index("ix_conversation_members_conversation_id", "conversation_members", ["conversation_id"], unique=False)
    op.create_index("ix_conversation_members_member_user_id", "conversation_members", ["member_user_id"], unique=False)
    op.create_index("ix_conversation_members_member_address", "conversation_members", ["member_address"], unique=False)
    op.create_index(
        "ix_conversation_members_member_server_onion",
        "conversation_members",
        ["member_server_onion"],
        unique=False,
    )
    op.create_index("ix_conversation_members_role", "conversation_members", ["role"], unique=False)
    op.create_index("ix_conversation_members_status", "conversation_members", ["status"], unique=False)

    op.create_table(
        "group_membership_events",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("group_uid", sa.String(length=64), nullable=False),
        sa.Column("event_seq", sa.Integer(), nullable=False),
        sa.Column("event_type", sa.String(length=24), nullable=False),
        sa.Column("actor_address", sa.String(length=320), nullable=False),
        sa.Column("target_address", sa.String(length=320), nullable=False),
        sa.Column("payload_json", sa.JSON(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint("group_uid", "event_seq", name="uq_group_membership_event_seq"),
    )
    op.create_index("ix_group_membership_events_group_uid", "group_membership_events", ["group_uid"], unique=False)
    op.create_index("ix_group_membership_events_event_seq", "group_membership_events", ["event_seq"], unique=False)
    op.create_index("ix_group_membership_events_event_type", "group_membership_events", ["event_type"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_group_membership_events_event_type", table_name="group_membership_events")
    op.drop_index("ix_group_membership_events_event_seq", table_name="group_membership_events")
    op.drop_index("ix_group_membership_events_group_uid", table_name="group_membership_events")
    op.drop_table("group_membership_events")

    op.drop_index("ix_conversation_members_status", table_name="conversation_members")
    op.drop_index("ix_conversation_members_role", table_name="conversation_members")
    op.drop_index("ix_conversation_members_member_server_onion", table_name="conversation_members")
    op.drop_index("ix_conversation_members_member_address", table_name="conversation_members")
    op.drop_index("ix_conversation_members_member_user_id", table_name="conversation_members")
    op.drop_index("ix_conversation_members_conversation_id", table_name="conversation_members")
    op.drop_table("conversation_members")

    op.drop_index("ix_conversations_group_uid", table_name="conversations")
    op.drop_index("ix_conversations_conversation_type", table_name="conversations")
    op.drop_column("conversations", "owner_address")
    op.drop_column("conversations", "origin_server_onion")
    op.drop_column("conversations", "group_name")
    op.drop_column("conversations", "group_uid")
    op.drop_column("conversations", "conversation_type")
