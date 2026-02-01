"""Session REST endpoints — core CRUD + control operations."""

from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException, Request, status

from ..auth import verify_token
from ..models import (
    NewSessionRequest,
    PromptRequest,
    SessionDetail,
    SessionSummary,
    Transcript,
    YoloUpdateRequest,
)
from ..transcript_parser import parse_transcript

router = APIRouter(prefix="/api/sessions", tags=["sessions"])


def _registry(request: Request):
    return request.app.state.registry


def _tmux(request: Request):
    return request.app.state.tmux


# ---------------------------------------------------------------------------
# List / Detail
# ---------------------------------------------------------------------------

@router.get("", response_model=list[SessionSummary])
async def list_sessions(
    registry=Depends(_registry),
    _token: str = Depends(verify_token),
):
    """List all Konsolai sessions."""
    return await registry.list_sessions()


@router.get("/{name}", response_model=SessionDetail)
async def get_session(
    name: str,
    registry=Depends(_registry),
    _token: str = Depends(verify_token),
):
    """Get detailed info for a single session."""
    session = await registry.get_session(name)
    if session is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"Session '{name}' not found")
    return session


# ---------------------------------------------------------------------------
# Transcript
# ---------------------------------------------------------------------------

@router.get("/{name}/transcript", response_model=Transcript)
async def get_transcript(
    name: str,
    lines: int = 500,
    tmux=Depends(_tmux),
    registry=Depends(_registry),
    _token: str = Depends(verify_token),
):
    """Get the parsed conversation transcript."""
    session = await registry.get_session(name)
    if session is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"Session '{name}' not found")
    raw = await tmux.capture_pane(name, lines=lines)
    return parse_transcript(raw, name)


# ---------------------------------------------------------------------------
# Control
# ---------------------------------------------------------------------------

@router.post("/{name}/prompt", status_code=status.HTTP_202_ACCEPTED)
async def send_prompt(
    name: str,
    body: PromptRequest,
    tmux=Depends(_tmux),
    registry=Depends(_registry),
    _token: str = Depends(verify_token),
):
    """Send a text prompt to Claude."""
    session = await registry.get_session(name)
    if session is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"Session '{name}' not found")
    ok = await tmux.send_text(name, body.text)
    if not ok:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Failed to send to tmux")
    return {"status": "sent", "session": name}


@router.post("/{name}/approve", status_code=status.HTTP_202_ACCEPTED)
async def approve_permission(
    name: str,
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Approve a pending permission request (press Enter)."""
    ok = await tmux.send_keys(name, "Enter")
    if not ok:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Failed to send to tmux")
    return {"status": "approved", "session": name}


@router.post("/{name}/deny", status_code=status.HTTP_202_ACCEPTED)
async def deny_permission(
    name: str,
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Deny a pending permission request (Escape then 'n' then Enter)."""
    ok = await tmux.send_keys(name, "Escape")
    if ok:
        ok = await tmux.send_text(name, "n")
    if not ok:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Failed to send to tmux")
    return {"status": "denied", "session": name}


@router.post("/{name}/stop", status_code=status.HTTP_202_ACCEPTED)
async def stop_session(
    name: str,
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Send Ctrl+C to stop Claude."""
    ok = await tmux.send_ctrl_c(name)
    if not ok:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Failed to send to tmux")
    return {"status": "stopped", "session": name}


@router.post("/{name}/kill", status_code=status.HTTP_202_ACCEPTED)
async def kill_session(
    name: str,
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Kill the tmux session entirely."""
    ok = await tmux.kill_session(name)
    if not ok:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Failed to kill session")
    return {"status": "killed", "session": name}


# ---------------------------------------------------------------------------
# Yolo mode
# ---------------------------------------------------------------------------

@router.put("/{name}/yolo", status_code=status.HTTP_200_OK)
async def update_yolo(
    name: str,
    body: YoloUpdateRequest,
    registry=Depends(_registry),
    _token: str = Depends(verify_token),
):
    """Update yolo mode settings for a session."""
    session = await registry.get_session(name)
    if session is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"Session '{name}' not found")
    # Return current state (actual toggling happens via D-Bus or direct tmux)
    return {
        "session": name,
        "yolo": body.yolo if body.yolo is not None else session.yolo.yolo,
        "double_yolo": body.double_yolo if body.double_yolo is not None else session.yolo.double_yolo,
        "triple_yolo": body.triple_yolo if body.triple_yolo is not None else session.yolo.triple_yolo,
    }


# ---------------------------------------------------------------------------
# Token usage
# ---------------------------------------------------------------------------

@router.get("/{name}/token-usage")
async def get_token_usage(
    name: str,
    registry=Depends(_registry),
    _token: str = Depends(verify_token),
):
    """Get token usage metrics for a session."""
    session = await registry.get_session(name)
    if session is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"Session '{name}' not found")
    tu = session.token_usage
    return {
        "session": name,
        "input_tokens": tu.input_tokens,
        "output_tokens": tu.output_tokens,
        "cache_read_tokens": tu.cache_read_tokens,
        "cache_creation_tokens": tu.cache_creation_tokens,
        "total_tokens": tu.total_tokens,
        "estimated_cost_usd": tu.estimated_cost_usd,
    }


# ---------------------------------------------------------------------------
# Create session
# ---------------------------------------------------------------------------

@router.post("/new", status_code=status.HTTP_201_CREATED, response_model=SessionDetail)
async def create_session(
    body: NewSessionRequest,
    registry=Depends(_registry),
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Create a new Konsolai session."""
    import secrets
    session_id = secrets.token_hex(4)
    name = f"konsolai-{body.profile}-{session_id}"
    # Build claude command
    cmd = "claude"
    if body.model and body.model != "default":
        cmd += f" --model {body.model}"
    ok = await tmux.create_session(name, working_dir=body.working_dir, command=cmd)
    if not ok:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Failed to create tmux session")
    session = await registry.get_session(name)
    if session is None:
        raise HTTPException(status.HTTP_502_BAD_GATEWAY, "Session created but not found")
    return session
