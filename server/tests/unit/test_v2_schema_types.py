from sqlalchemy import BigInteger

from app.models.message_event import MessageEvent


def test_message_event_sent_at_ms_is_bigint() -> None:
    column_type = MessageEvent.__table__.c.sent_at_ms.type
    assert isinstance(column_type, BigInteger)
