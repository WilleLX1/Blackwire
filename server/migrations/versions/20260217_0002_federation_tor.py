"""federation and tor-ready schema

Revision ID: 20260217_0002
Revises: 20260213_0001
Create Date: 2026-02-17 18:15:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260217_0002"
down_revision: str | None = "20260213_0001"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.add_column(
        "conversations",
        sa.Column("kind", sa.String(length=16), nullable=False, server_default="local"),
    )
    op.add_column(
        "conversations",
        sa.Column("local_user_id", sa.String(length=36), sa.ForeignKey("users.id", ondelete="CASCADE"), nullable=True),
    )
    op.add_column(
        "conversations",
        sa.Column("peer_username", sa.String(length=64), nullable=False, server_default=""),
    )
    op.add_column(
        "conversations",
        sa.Column("peer_server_onion", sa.String(length=255), nullable=False, server_default=""),
    )
    op.add_column(
        "conversations",
        sa.Column("peer_address", sa.String(length=320), nullable=False, server_default=""),
    )
    op.create_index("ix_conversations_kind", "conversations", ["kind"], unique=False)
    op.create_index("ix_conversations_local_user_id", "conversations", ["local_user_id"], unique=False)
    op.create_index("ix_conversations_peer_address", "conversations", ["peer_address"], unique=False)
    op.drop_constraint("uq_conversation_user_pair", "conversations", type_="unique")
    op.create_unique_constraint(
        "uq_conversation_local_pair",
        "conversations",
        ["kind", "user_a_id", "user_b_id"],
    )
    op.create_unique_constraint(
        "uq_conversation_remote_peer",
        "conversations",
        ["kind", "local_user_id", "peer_address"],
    )

    with op.batch_alter_table("messages") as batch:
        batch.add_column(sa.Column("sender_address", sa.String(length=320), nullable=False, server_default=""))
        batch.alter_column("sender_user_id", existing_type=sa.String(length=36), nullable=True)
        batch.alter_column("sender_device_id", existing_type=sa.String(length=36), type_=sa.String(length=128))

    # sender_device_id used to be a FK to devices.id; federation requires opaque remote sender device ids.
    try:
        op.drop_constraint("messages_sender_device_id_fkey", "messages", type_="foreignkey")
    except Exception:
        pass
    op.create_index("ix_messages_sender_address", "messages", ["sender_address"], unique=False)

    op.execute(
        """
        UPDATE messages
        SET sender_address = COALESCE(
            (SELECT u.username FROM users u WHERE u.id = messages.sender_user_id),
            'unknown'
        ) || '@legacy.local'
        WHERE sender_address = '' OR sender_address IS NULL
        """
    )

    op.create_table(
        "federation_peers",
        sa.Column("onion", sa.String(length=255), primary_key=True),
        sa.Column("signing_public_key", sa.String(length=256), nullable=False),
        sa.Column("first_seen_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("last_seen_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("status", sa.String(length=32), nullable=False),
    )
    op.create_index("ix_federation_peers_status", "federation_peers", ["status"], unique=False)

    op.create_table(
        "federation_nonce_replay",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("peer_onion", sa.String(length=255), nullable=False),
        sa.Column("nonce", sa.String(length=128), nullable=False),
        sa.Column("seen_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint("peer_onion", "nonce", name="uq_federation_peer_nonce"),
    )
    op.create_index(
        "ix_federation_nonce_replay_peer_onion",
        "federation_nonce_replay",
        ["peer_onion"],
        unique=False,
    )
    op.create_index("ix_federation_nonce_replay_nonce", "federation_nonce_replay", ["nonce"], unique=False)
    op.create_index(
        "ix_federation_nonce_replay_expires_at",
        "federation_nonce_replay",
        ["expires_at"],
        unique=False,
    )

    op.create_table(
        "federation_outbox",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("peer_onion", sa.String(length=255), nullable=False),
        sa.Column("event_type", sa.String(length=64), nullable=False),
        sa.Column("endpoint_path", sa.String(length=255), nullable=False),
        sa.Column("http_method", sa.String(length=16), nullable=False),
        sa.Column("dedupe_key", sa.String(length=255), nullable=False),
        sa.Column("payload_json", sa.JSON(), nullable=False),
        sa.Column("status", sa.String(length=32), nullable=False),
        sa.Column("attempt_count", sa.Integer(), nullable=False),
        sa.Column("next_attempt_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("last_attempt_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("last_http_status", sa.Integer(), nullable=True),
        sa.Column("last_error", sa.String(length=512), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.UniqueConstraint("dedupe_key", name="uq_federation_outbox_dedupe_key"),
    )
    op.create_index("ix_federation_outbox_peer_onion", "federation_outbox", ["peer_onion"], unique=False)
    op.create_index("ix_federation_outbox_event_type", "federation_outbox", ["event_type"], unique=False)
    op.create_index("ix_federation_outbox_status", "federation_outbox", ["status"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_federation_outbox_status", table_name="federation_outbox")
    op.drop_index("ix_federation_outbox_event_type", table_name="federation_outbox")
    op.drop_index("ix_federation_outbox_peer_onion", table_name="federation_outbox")
    op.drop_table("federation_outbox")

    op.drop_index("ix_federation_nonce_replay_expires_at", table_name="federation_nonce_replay")
    op.drop_index("ix_federation_nonce_replay_nonce", table_name="federation_nonce_replay")
    op.drop_index("ix_federation_nonce_replay_peer_onion", table_name="federation_nonce_replay")
    op.drop_table("federation_nonce_replay")

    op.drop_index("ix_federation_peers_status", table_name="federation_peers")
    op.drop_table("federation_peers")

    op.drop_index("ix_messages_sender_address", table_name="messages")
    with op.batch_alter_table("messages") as batch:
        batch.alter_column("sender_device_id", existing_type=sa.String(length=128), type_=sa.String(length=36))
        batch.alter_column("sender_user_id", existing_type=sa.String(length=36), nullable=False)
        batch.drop_column("sender_address")
    op.create_foreign_key(
        "messages_sender_device_id_fkey",
        "messages",
        "devices",
        ["sender_device_id"],
        ["id"],
        ondelete="CASCADE",
    )

    op.drop_constraint("uq_conversation_remote_peer", "conversations", type_="unique")
    op.drop_constraint("uq_conversation_local_pair", "conversations", type_="unique")
    op.create_unique_constraint("uq_conversation_user_pair", "conversations", ["user_a_id", "user_b_id"])
    op.drop_index("ix_conversations_peer_address", table_name="conversations")
    op.drop_index("ix_conversations_local_user_id", table_name="conversations")
    op.drop_index("ix_conversations_kind", table_name="conversations")
    op.drop_column("conversations", "peer_address")
    op.drop_column("conversations", "peer_server_onion")
    op.drop_column("conversations", "peer_username")
    op.drop_column("conversations", "local_user_id")
    op.drop_column("conversations", "kind")
