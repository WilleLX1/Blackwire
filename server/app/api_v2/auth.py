from fastapi import APIRouter, Depends, HTTPException, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key, user_rate_limit_key
from app.dependencies import db_session, get_bootstrap_user_v2
from app.models.user import User
from app.schemas.user import UserOut
from app.schemas.v2_auth import (
    BindDeviceRequestV2,
    BootstrapAuthResponseV2,
    DeviceAuthResponseV2,
    LoginRequestV2,
    LogoutRequestV2,
    RefreshRequestV2,
    RegisterRequestV2,
)
from app.services.auth_service import AuthServiceError
from app.services.auth_service_v2 import auth_service_v2
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/api/v2/auth", tags=["auth-v2"])


@router.post("/register", response_model=BootstrapAuthResponseV2, status_code=status.HTTP_201_CREATED)
async def register(
    payload: RegisterRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> BootstrapAuthResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-auth-register"))
    try:
        user = await auth_service_v2.register(session, payload.username, payload.password)
        tokens = await auth_service_v2.issue_bootstrap(user)
        return BootstrapAuthResponseV2(user=UserOut.from_user(user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/login", response_model=BootstrapAuthResponseV2)
async def login(
    payload: LoginRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> BootstrapAuthResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-auth-login"))
    await rate_limiter.enforce(user_rate_limit_key("v2-auth-login", payload.username), limit=10)
    try:
        user = await auth_service_v2.authenticate(session, payload.username, payload.password)
        tokens = await auth_service_v2.issue_bootstrap(user)
        return BootstrapAuthResponseV2(user=UserOut.from_user(user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/bind-device", response_model=DeviceAuthResponseV2)
async def bind_device(
    payload: BindDeviceRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
    bootstrap_user: User = Depends(get_bootstrap_user_v2),
) -> DeviceAuthResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-auth-bind-device"))
    try:
        tokens = await auth_service_v2.bind_device(session, bootstrap_user, payload)
        return DeviceAuthResponseV2(user=UserOut.from_user(bootstrap_user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/refresh", response_model=DeviceAuthResponseV2)
async def refresh(
    payload: RefreshRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> DeviceAuthResponseV2:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-auth-refresh"))
    try:
        user, tokens = await auth_service_v2.refresh(session, payload.refresh_token)
        return DeviceAuthResponseV2(user=UserOut.from_user(user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/logout", status_code=status.HTTP_204_NO_CONTENT)
async def logout(
    payload: LogoutRequestV2,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> None:
    await rate_limiter.enforce(client_rate_limit_key(request, "v2-auth-logout"))
    await auth_service_v2.logout(session, payload.refresh_token)

