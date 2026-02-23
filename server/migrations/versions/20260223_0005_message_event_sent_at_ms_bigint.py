"""message_events.sent_at_ms bigint

Revision ID: 20260223_0005
Revises: 20260223_0004
Create Date: 2026-02-23 16:20:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260223_0005"
down_revision: str | None = "20260223_0004"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.alter_column(
        "message_events",
        "sent_at_ms",
        existing_type=sa.Integer(),
        type_=sa.BigInteger(),
        existing_nullable=False,
    )


def downgrade() -> None:
    op.alter_column(
        "message_events",
        "sent_at_ms",
        existing_type=sa.BigInteger(),
        type_=sa.Integer(),
        existing_nullable=False,
    )
