"""allow nullable local pair columns for remote conversations

Revision ID: 20260217_0003
Revises: 20260217_0002
Create Date: 2026-02-17 20:05:00
"""

from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = "20260217_0003"
down_revision: str | None = "20260217_0002"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    with op.batch_alter_table("conversations") as batch:
        batch.alter_column("user_a_id", existing_type=sa.String(length=36), nullable=True)
        batch.alter_column("user_b_id", existing_type=sa.String(length=36), nullable=True)


def downgrade() -> None:
    op.execute(
        """
        UPDATE conversations
        SET
            user_a_id = COALESCE(user_a_id, local_user_id),
            user_b_id = COALESCE(user_b_id, local_user_id)
        WHERE user_a_id IS NULL OR user_b_id IS NULL
        """
    )
    with op.batch_alter_table("conversations") as batch:
        batch.alter_column("user_a_id", existing_type=sa.String(length=36), nullable=False)
        batch.alter_column("user_b_id", existing_type=sa.String(length=36), nullable=False)
