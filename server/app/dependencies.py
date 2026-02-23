from collections.abc import AsyncGenerator
from dataclasses import dataclass

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.db import get_db_session
from app.models.device import Device
from app.models.user import User
from app.security.tokens import TokenError, decode_token
from app.security.tokens_v2 import TokenErrorV2, decode_token as decode_token_v2

bearer_scheme = HTTPBearer(auto_error=False)
bootstrap_bearer_scheme = HTTPBearer(auto_error=False)


@dataclass(slots=True)
class AuthenticatedDeviceContextV2:
    user: User
    device: Device


async def db_session() -> AsyncGenerator[AsyncSession, None]:
    async for session in get_db_session():
        yield session


async def get_current_user(
    credentials: HTTPAuthorizationCredentials | None = Depends(bearer_scheme),
    session: AsyncSession = Depends(db_session),
) -> User:
    if credentials is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing auth token")

    token = credentials.credentials
    try:
        payload = decode_token(token, expected_type="access")
    except TokenError as exc:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(exc)) from exc

    user_id = payload.get("sub")
    if not user_id:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid access token")

    stmt = select(User).where(User.id == user_id, User.disabled_at.is_(None))
    result = await session.execute(stmt)
    user = result.scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found")
    return user


async def get_bootstrap_user_v2(
    credentials: HTTPAuthorizationCredentials | None = Depends(bootstrap_bearer_scheme),
    session: AsyncSession = Depends(db_session),
) -> User:
    if credentials is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing auth token")

    token = credentials.credentials
    try:
        payload = decode_token_v2(token, expected_type="bootstrap")
    except TokenErrorV2 as exc:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(exc)) from exc

    user_id = payload.get("sub")
    if not user_id:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid bootstrap token")

    stmt = select(User).where(User.id == user_id, User.disabled_at.is_(None))
    result = await session.execute(stmt)
    user = result.scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found")
    return user


async def get_current_device_context_v2(
    credentials: HTTPAuthorizationCredentials | None = Depends(bearer_scheme),
    session: AsyncSession = Depends(db_session),
) -> AuthenticatedDeviceContextV2:
    if credentials is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing auth token")

    token = credentials.credentials
    try:
        payload = decode_token_v2(token, expected_type="access")
    except TokenErrorV2 as exc:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(exc)) from exc

    user_id = payload.get("sub")
    device_uid = payload.get("did")
    if not user_id or not device_uid:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid access token")

    user_stmt = select(User).where(User.id == user_id, User.disabled_at.is_(None))
    user = (await session.execute(user_stmt)).scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found")

    device_stmt = select(Device).where(Device.id == str(device_uid), Device.user_id == user.id)
    device = (await session.execute(device_stmt)).scalar_one_or_none()
    if device is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Device not found")
    if device.status != "active" or device.revoked_at is not None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Device revoked")

    return AuthenticatedDeviceContextV2(user=user, device=device)
