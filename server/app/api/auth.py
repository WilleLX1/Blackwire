from fastapi import APIRouter, Depends, HTTPException, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.utils import client_rate_limit_key
from app.dependencies import db_session
from app.schemas.auth import (
    AuthResponse,
    LoginRequest,
    LogoutRequest,
    RefreshRequest,
    RegisterRequest,
)
from app.schemas.user import UserOut
from app.services.auth_service import AuthServiceError, auth_service
from app.services.rate_limit import rate_limiter

router = APIRouter(prefix="/auth", tags=["auth"])


@router.post("/register", response_model=AuthResponse, status_code=status.HTTP_201_CREATED)
async def register(
    payload: RegisterRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> AuthResponse:
    await rate_limiter.enforce(client_rate_limit_key(request, "auth-register"))
    try:
        user = await auth_service.register(session, payload.username, payload.password)
        tokens = await auth_service.issue_tokens(session, user)
        return AuthResponse(user=UserOut.from_user(user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/login", response_model=AuthResponse)
async def login(
    payload: LoginRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> AuthResponse:
    await rate_limiter.enforce(client_rate_limit_key(request, "auth-login"))
    try:
        user = await auth_service.authenticate(session, payload.username, payload.password)
        tokens = await auth_service.issue_tokens(session, user)
        return AuthResponse(user=UserOut.from_user(user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/refresh", response_model=AuthResponse)
async def refresh(
    payload: RefreshRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> AuthResponse:
    await rate_limiter.enforce(client_rate_limit_key(request, "auth-refresh"))
    try:
        user, tokens = await auth_service.refresh(session, payload.refresh_token)
        return AuthResponse(user=UserOut.from_user(user), tokens=tokens)
    except AuthServiceError as exc:
        raise HTTPException(status_code=exc.status_code, detail=str(exc)) from exc


@router.post("/logout", status_code=status.HTTP_204_NO_CONTENT)
async def logout(
    payload: LogoutRequest,
    request: Request,
    session: AsyncSession = Depends(db_session),
) -> None:
    await rate_limiter.enforce(client_rate_limit_key(request, "auth-logout"))
    await auth_service.logout(session, payload.refresh_token)
