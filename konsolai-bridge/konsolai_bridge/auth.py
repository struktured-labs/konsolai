"""Authentication middleware — bearer-token based."""

from __future__ import annotations

from fastapi import Depends, HTTPException, Request, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from .config import BridgeConfig

_security = HTTPBearer(auto_error=False)


def get_config(request: Request) -> BridgeConfig:
    """Retrieve the BridgeConfig stored on app.state."""
    return request.app.state.config


async def verify_token(
    creds: HTTPAuthorizationCredentials | None = Depends(_security),
    config: BridgeConfig = Depends(get_config),
) -> str:
    """Validate the bearer token.  Returns the token on success."""
    if not config.bearer_token:
        # No token configured — auth disabled (local-only mode)
        return ""
    if creds is None or creds.credentials != config.bearer_token:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or missing bearer token",
        )
    return creds.credentials
