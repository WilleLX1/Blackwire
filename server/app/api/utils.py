from fastapi import Request


def client_rate_limit_key(request: Request, scope: str) -> str:
    host = request.client.host if request.client else "unknown"
    return f"{scope}:{host}"


def user_rate_limit_key(scope: str, username: str) -> str:
    """Rate limit key scoped to a specific username (for brute-force protection)."""
    return f"{scope}:user:{username.strip().lower()}"
