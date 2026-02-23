"""v2 ratchet core schema

Revision ID: 20260223_0006
Revises: 20260223_0005
Create Date: 2026-02-23 18:00:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260223_0006"
down_revision: str | None = "20260223_0005"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "device_signed_prekeys",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("device_id", sa.String(length=36), sa.ForeignKey("devices.id", ondelete="CASCADE"), nullable=False),
        sa.Column("key_id", sa.Integer(), nullable=False),
        sa.Column("pub_x25519_b64", sa.String(length=128), nullable=False),
        sa.Column("sig_by_device_sign_key_b64", sa.String(length=256), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("revoked_at", sa.DateTime(timezone=True), nullable=True),
        sa.UniqueConstraint("device_id", "key_id", name="uq_device_signed_prekey_device_key"),
    )
    op.create_index("ix_device_signed_prekeys_device_id", "device_signed_prekeys", ["device_id"], unique=False)
    op.create_index(
        "ix_device_signed_prekeys_device_exp_revoked",
        "device_signed_prekeys",
        ["device_id", "expires_at", "revoked_at"],
        unique=False,
    )

    op.create_table(
        "device_one_time_prekeys",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("device_id", sa.String(length=36), sa.ForeignKey("devices.id", ondelete="CASCADE"), nullable=False),
        sa.Column("key_id", sa.Integer(), nullable=False),
        sa.Column("pub_x25519_b64", sa.String(length=128), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("consumed_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("consumed_by_address", sa.String(length=320), nullable=True),
        sa.UniqueConstraint("device_id", "key_id", name="uq_device_one_time_prekey_device_key"),
    )
    op.create_index("ix_device_one_time_prekeys_device_id", "device_one_time_prekeys", ["device_id"], unique=False)
    op.create_index(
        "ix_device_one_time_prekeys_device_consumed",
        "device_one_time_prekeys",
        ["device_id", "consumed_at"],
        unique=False,
    )

    op.create_table(
        "ratchet_sessions",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("owner_user_id", sa.String(length=36), sa.ForeignKey("users.id", ondelete="CASCADE"), nullable=False),
        sa.Column("owner_device_uid", sa.String(length=36), sa.ForeignKey("devices.id", ondelete="CASCADE"), nullable=False),
        sa.Column("peer_address", sa.String(length=320), nullable=False),
        sa.Column("peer_device_uid", sa.String(length=64), nullable=False),
        sa.Column("session_version", sa.String(length=16), nullable=False, server_default="dr_v1"),
        sa.Column("state_blob_encrypted_b64", sa.String(length=65536), nullable=False, server_default=""),
        sa.Column("state_nonce_b64", sa.String(length=128), nullable=False, server_default=""),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("last_send_chain_n", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("last_recv_chain_n", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("last_root_key_hash", sa.String(length=128), nullable=False, server_default=""),
        sa.UniqueConstraint(
            "owner_device_uid",
            "peer_address",
            "peer_device_uid",
            name="uq_ratchet_session_owner_peer",
        ),
    )
    op.create_index("ix_ratchet_sessions_owner_user_id", "ratchet_sessions", ["owner_user_id"], unique=False)
    op.create_index("ix_ratchet_sessions_owner_device_uid", "ratchet_sessions", ["owner_device_uid"], unique=False)
    op.create_index("ix_ratchet_sessions_peer_address", "ratchet_sessions", ["peer_address"], unique=False)
    op.create_index("ix_ratchet_sessions_peer_device_uid", "ratchet_sessions", ["peer_device_uid"], unique=False)

    op.create_table(
        "ratchet_skipped_keys",
        sa.Column("id", sa.String(length=36), primary_key=True),
        sa.Column("owner_device_uid", sa.String(length=36), sa.ForeignKey("devices.id", ondelete="CASCADE"), nullable=False),
        sa.Column("peer_address", sa.String(length=320), nullable=False),
        sa.Column("peer_device_uid", sa.String(length=64), nullable=False),
        sa.Column("dh_pub_b64", sa.String(length=128), nullable=False),
        sa.Column("msg_n", sa.Integer(), nullable=False),
        sa.Column("mk_encrypted_b64", sa.String(length=512), nullable=False),
        sa.Column("mk_nonce_b64", sa.String(length=128), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("used_at", sa.DateTime(timezone=True), nullable=True),
        sa.UniqueConstraint(
            "owner_device_uid",
            "peer_address",
            "peer_device_uid",
            "dh_pub_b64",
            "msg_n",
            name="uq_ratchet_skipped_owner_peer_dh_n",
        ),
    )
    op.create_index("ix_ratchet_skipped_keys_owner_device_uid", "ratchet_skipped_keys", ["owner_device_uid"], unique=False)
    op.create_index("ix_ratchet_skipped_keys_peer_address", "ratchet_skipped_keys", ["peer_address"], unique=False)
    op.create_index("ix_ratchet_skipped_keys_peer_device_uid", "ratchet_skipped_keys", ["peer_device_uid"], unique=False)

    with op.batch_alter_table("message_events") as batch:
        batch.add_column(sa.Column("encryption_mode", sa.String(length=32), nullable=False, server_default="sealedbox_v0_2a"))
    op.create_index("ix_message_events_encryption_mode", "message_events", ["encryption_mode"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_message_events_encryption_mode", table_name="message_events")
    with op.batch_alter_table("message_events") as batch:
        batch.drop_column("encryption_mode")

    op.drop_index("ix_ratchet_skipped_keys_peer_device_uid", table_name="ratchet_skipped_keys")
    op.drop_index("ix_ratchet_skipped_keys_peer_address", table_name="ratchet_skipped_keys")
    op.drop_index("ix_ratchet_skipped_keys_owner_device_uid", table_name="ratchet_skipped_keys")
    op.drop_table("ratchet_skipped_keys")

    op.drop_index("ix_ratchet_sessions_peer_device_uid", table_name="ratchet_sessions")
    op.drop_index("ix_ratchet_sessions_peer_address", table_name="ratchet_sessions")
    op.drop_index("ix_ratchet_sessions_owner_device_uid", table_name="ratchet_sessions")
    op.drop_index("ix_ratchet_sessions_owner_user_id", table_name="ratchet_sessions")
    op.drop_table("ratchet_sessions")

    op.drop_index("ix_device_one_time_prekeys_device_consumed", table_name="device_one_time_prekeys")
    op.drop_index("ix_device_one_time_prekeys_device_id", table_name="device_one_time_prekeys")
    op.drop_table("device_one_time_prekeys")

    op.drop_index("ix_device_signed_prekeys_device_exp_revoked", table_name="device_signed_prekeys")
    op.drop_index("ix_device_signed_prekeys_device_id", table_name="device_signed_prekeys")
    op.drop_table("device_signed_prekeys")
