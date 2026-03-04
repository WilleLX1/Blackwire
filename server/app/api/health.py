from fastapi import APIRouter, status
from fastapi.responses import JSONResponse
from sqlalchemy import text

from app.db import check_db_health, get_engine

router = APIRouter(prefix="/health", tags=["health"])


@router.get("/live")
async def liveness() -> dict[str, str]:
    return {"status": "ok"}


@router.get("/ready")
async def readiness() -> JSONResponse:
    db_ok = await check_db_health()
    migration_version = None

    if db_ok:
        engine = get_engine()
        try:
            async with engine.connect() as conn:
                result = await conn.execute(text("SELECT version_num FROM alembic_version LIMIT 1"))
                migration_version = result.scalar_one_or_none()
        except Exception:
            migration_version = None

    is_ready = db_ok and migration_version is not None
    payload = {
        "status": "ready" if is_ready else "not_ready",
        "database": db_ok,
    }
    return JSONResponse(payload, status_code=status.HTTP_200_OK if is_ready else status.HTTP_503_SERVICE_UNAVAILABLE)
