from datetime import UTC, datetime, timedelta

from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.federation_outbox import FederationOutbox
from app.services.federation_client import FederationClientError, federation_client


class FederationOutboxService:
    async def enqueue(
        self,
        session: AsyncSession,
        *,
        peer_onion: str,
        event_type: str,
        endpoint_path: str,
        payload_json: dict,
        dedupe_key: str,
    ) -> FederationOutbox:
        existing_stmt = select(FederationOutbox).where(FederationOutbox.dedupe_key == dedupe_key)
        existing = (await session.execute(existing_stmt)).scalar_one_or_none()
        if existing is not None:
            return existing

        row = FederationOutbox(
            peer_onion=peer_onion.strip().lower(),
            event_type=event_type,
            endpoint_path=endpoint_path,
            http_method="POST",
            payload_json=payload_json,
            dedupe_key=dedupe_key,
            status="pending",
        )
        session.add(row)
        await session.flush()
        return row

    async def deliver_item(self, session: AsyncSession, outbox_id: str) -> None:
        row = (
            await session.execute(select(FederationOutbox).where(FederationOutbox.id == outbox_id))
        ).scalar_one_or_none()
        if row is None or row.status == "sent":
            return

        row.attempt_count += 1
        row.last_attempt_at = datetime.now(UTC)
        try:
            await federation_client.post_signed(
                row.peer_onion,
                row.endpoint_path,
                row.payload_json,
            )
            row.status = "sent"
            row.last_http_status = 200
            row.last_error = None
        except FederationClientError as exc:
            row.status = "pending"
            row.last_http_status = exc.status_code
            row.last_error = exc.detail[:512]
            backoff_seconds = min(300, 2 ** min(8, row.attempt_count))
            row.next_attempt_at = datetime.now(UTC) + timedelta(seconds=backoff_seconds)

        await session.commit()

    async def process_due(self, session: AsyncSession, limit: int = 50) -> int:
        now = datetime.now(UTC)
        stmt = (
            select(FederationOutbox)
            .where(
                FederationOutbox.status == "pending",
                FederationOutbox.next_attempt_at <= now,
            )
            .order_by(FederationOutbox.next_attempt_at.asc())
            .limit(limit)
        )
        rows = list((await session.execute(stmt)).scalars().all())
        processed = 0
        for row in rows:
            try:
                await self.deliver_item(session, row.id)
            except IntegrityError:
                await session.rollback()
            processed += 1
        return processed


federation_outbox_service = FederationOutboxService()
