"""v2 security core schema

Revision ID: 20260223_0004
Revises: 20260217_0003
Create Date: 2026-02-23 15:20:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260223_0004"
down_revision: str | None = "20260217_0003"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    with op.batch_alter_table("devices") as batch:
        batch.add_column(sa.Column("status", sa.String(length=16), nullable=False, server_default="active"))
        batch.add_column(
            sa.Column(
                "last_seen_at",
                sa.DateTime(timezone=True),
                nullable=False,
                server_default=sa.text("CURRENT_TIMESTAMP"),
            )
        )
    op.create_index("ix_devices_status", "devices", ["status"], unique=False)

    with op.batch_alter_table("refresh_tokens") as batch:
        batch.add_column(sa.Column("device_id", sa.String(length=36), nullable=True))
        batch.add_column(sa.Column("token_kind", sa.String(length=16), nullable=False, server_default="refresh"))
        batch.create_foreign_key(
            "refresh_tokens_device_id_fkey",
            "devices",
            ["device_id"],
            ["id"],
            ondelete="CASCADE",
        )
    op.create_index("ix_refresh_tokens_device_id", "refresh_tokens", ["device_id"], unique=False)
    op.create_index("ix_refresh_tokens_token_kind", "refresh_tokens", ["token_kind"], unique=False)
    op.create_index(
        "ix_refresh_tokens_user_device_revoked",
        "refresh_tokens",
        ["user_id", "device_id", "revoked_at"],
        unique=False,
    )

    op.create_table(
        "message_events",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column(
            "conversation_id",
            sa.String(length=36),
            sa.ForeignKey("conversations.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("sender_user_id", sa.String(length=36), sa.ForeignKey("users.id", ondelete="SET NULL"), nullable=True),
        sa.Column("sender_address", sa.String(length=320), nullable=False),
        sa.Column("sender_device_uid", sa.String(length=36), nullable=False),
        sa.Column("sender_device_pubkey", sa.String(length=256), nullable=False),
        sa.Column("client_message_id", sa.String(length=64), nullable=False),
        sa.Column("sent_at_ms", sa.Integer(), nullable=False),
        sa.Column("sender_prev_hash", sa.String(length=128), nullable=False, server_default=""),
        sa.Column("sender_chain_hash", sa.String(length=128), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint(
            "sender_device_uid",
            "client_message_id",
            name="uq_message_event_sender_client_message",
        ),
    )
    op.create_index("ix_message_events_conversation_id", "message_events", ["conversation_id"], unique=False)
    op.create_index("ix_message_events_sender_user_id", "message_events", ["sender_user_id"], unique=False)
    op.create_index("ix_message_events_sender_address", "message_events", ["sender_address"], unique=False)
    op.create_index("ix_message_events_sender_device_uid", "message_events", ["sender_device_uid"], unique=False)
    op.create_index("ix_message_events_client_message_id", "message_events", ["client_message_id"], unique=False)
    op.create_index("ix_message_events_sender_chain_hash", "message_events", ["sender_chain_hash"], unique=False)
    op.create_index("ix_message_events_sent_at_ms", "message_events", ["sent_at_ms"], unique=False)

    op.create_table(
        "message_device_copies",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column(
            "message_event_id",
            sa.String(length=36),
            sa.ForeignKey("message_events.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column(
            "recipient_user_id",
            sa.String(length=36),
            sa.ForeignKey("users.id", ondelete="CASCADE"),
            nullable=False,
        ),
        sa.Column("recipient_device_uid", sa.String(length=36), nullable=False),
        sa.Column("envelope_json", sa.JSON(), nullable=False),
        sa.Column("status", sa.String(length=20), nullable=False, server_default="pending"),
        sa.Column("available_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("delivered_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("attempt_count", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("last_attempt_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint(
            "message_event_id",
            "recipient_device_uid",
            name="uq_message_copy_event_recipient_device",
        ),
    )
    op.create_index("ix_message_device_copies_message_event_id", "message_device_copies", ["message_event_id"], unique=False)
    op.create_index("ix_message_device_copies_recipient_user_id", "message_device_copies", ["recipient_user_id"], unique=False)
    op.create_index(
        "ix_message_device_copies_recipient_device_uid",
        "message_device_copies",
        ["recipient_device_uid"],
        unique=False,
    )
    op.create_index("ix_message_device_copies_status", "message_device_copies", ["status"], unique=False)
    op.create_index("ix_message_device_copies_expires_at", "message_device_copies", ["expires_at"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_message_device_copies_expires_at", table_name="message_device_copies")
    op.drop_index("ix_message_device_copies_status", table_name="message_device_copies")
    op.drop_index("ix_message_device_copies_recipient_device_uid", table_name="message_device_copies")
    op.drop_index("ix_message_device_copies_recipient_user_id", table_name="message_device_copies")
    op.drop_index("ix_message_device_copies_message_event_id", table_name="message_device_copies")
    op.drop_table("message_device_copies")

    op.drop_index("ix_message_events_sent_at_ms", table_name="message_events")
    op.drop_index("ix_message_events_sender_chain_hash", table_name="message_events")
    op.drop_index("ix_message_events_client_message_id", table_name="message_events")
    op.drop_index("ix_message_events_sender_device_uid", table_name="message_events")
    op.drop_index("ix_message_events_sender_address", table_name="message_events")
    op.drop_index("ix_message_events_sender_user_id", table_name="message_events")
    op.drop_index("ix_message_events_conversation_id", table_name="message_events")
    op.drop_table("message_events")

    op.drop_index("ix_refresh_tokens_user_device_revoked", table_name="refresh_tokens")
    op.drop_index("ix_refresh_tokens_token_kind", table_name="refresh_tokens")
    op.drop_index("ix_refresh_tokens_device_id", table_name="refresh_tokens")
    with op.batch_alter_table("refresh_tokens") as batch:
        batch.drop_constraint("refresh_tokens_device_id_fkey", type_="foreignkey")
        batch.drop_column("token_kind")
        batch.drop_column("device_id")

    op.drop_index("ix_devices_status", table_name="devices")
    with op.batch_alter_table("devices") as batch:
        batch.drop_column("last_seen_at")
        batch.drop_column("status")

