"""Vehicle integration endpoints — Android Auto and CarPlay.

Provides simplified, safety-constrained APIs for vehicle head-unit displays
and voice-first interaction while driving.

Design constraints:
  - Short text labels (max 30 chars for session names, 20 for states)
  - Limited session count (configurable, default 5)
  - Voice command routing with TTS-friendly responses
  - No complex interactions — approve/deny/stop only
"""

from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException, Request, status

from ..auth import verify_token
from ..models import (
    ClaudeState,
    VehicleDashboard,
    VehicleSessionCard,
    VoiceCommandRequest,
    VoiceCommandResponse,
)

router = APIRouter(prefix="/api/vehicle", tags=["vehicle"])


def _registry(request: Request):
    return request.app.state.registry


def _tmux(request: Request):
    return request.app.state.tmux


def _config(request: Request):
    return request.app.state.config


_STATE_LABELS = {
    ClaudeState.NOT_RUNNING: "Stopped",
    ClaudeState.STARTING: "Starting",
    ClaudeState.IDLE: "Ready",
    ClaudeState.WORKING: "Working",
    ClaudeState.WAITING_INPUT: "Needs Input",
    ClaudeState.ERROR: "Error",
}

_ATTENTION_REASONS = {
    ClaudeState.WAITING_INPUT: "Permission needed",
    ClaudeState.ERROR: "Error occurred",
}


# ---------------------------------------------------------------------------
# Dashboard
# ---------------------------------------------------------------------------

@router.get("/dashboard", response_model=VehicleDashboard)
async def vehicle_dashboard(
    registry=Depends(_registry),
    config=Depends(_config),
    _token: str = Depends(verify_token),
):
    """Get simplified session data for vehicle display.

    Returns at most `vehicle_session_limit` sessions, prioritizing
    those needing attention.
    """
    sessions = await registry.list_sessions()
    limit = config.vehicle_session_limit

    cards: list[VehicleSessionCard] = []
    needing_attention = 0
    for s in sessions[:limit]:
        needs = s.state in (ClaudeState.WAITING_INPUT, ClaudeState.ERROR)
        if needs:
            needing_attention += 1
        # Truncate name for vehicle display
        display_name = s.name
        if len(display_name) > 30:
            display_name = display_name[:27] + "..."
        cost = f"${s.token_usage.estimated_cost_usd:.2f}" if s.token_usage.total_tokens > 0 else ""
        cards.append(VehicleSessionCard(
            name=display_name,
            state=s.state,
            state_label=_STATE_LABELS.get(s.state, "Unknown"),
            needs_attention=needs,
            attention_reason=_ATTENTION_REASONS.get(s.state, ""),
            cost_label=cost,
        ))

    total_active = sum(1 for s in sessions if s.state not in (ClaudeState.NOT_RUNNING,))
    summary = _build_summary(total_active, needing_attention)

    return VehicleDashboard(
        sessions=cards,
        total_active=total_active,
        total_needing_attention=needing_attention,
        summary_text=summary,
    )


# ---------------------------------------------------------------------------
# Voice commands
# ---------------------------------------------------------------------------

@router.post("/voice", response_model=VoiceCommandResponse)
async def handle_voice_command(
    body: VoiceCommandRequest,
    registry=Depends(_registry),
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Process a voice command from Android Auto or CarPlay.

    Supported commands:
      - "approve" / "yes" — approve permission on the specified or first waiting session
      - "deny" / "no" — deny permission
      - "stop" — send Ctrl+C
      - "status" — get current session status
      - "list sessions" — list active sessions
      - Anything else — sent as a prompt to the specified session
    """
    text = body.text.strip().lower()

    # Determine target session
    target = body.session_name
    sessions = await registry.list_sessions()

    if not target:
        # Auto-select: first session needing attention, or first active
        waiting = [s for s in sessions if s.needs_attention]
        active = [s for s in sessions if s.state != ClaudeState.NOT_RUNNING]
        if waiting:
            target = waiting[0].name
        elif active:
            target = active[0].name

    # --- Command routing ---

    if text in ("approve", "yes", "accept", "allow"):
        return await _voice_approve(target, tmux, registry)

    if text in ("deny", "no", "reject", "block"):
        return await _voice_deny(target, tmux, registry)

    if text in ("stop", "cancel", "halt"):
        return await _voice_stop(target, tmux)

    if text in ("status", "what's happening", "whats happening"):
        return _voice_status(sessions)

    if text in ("list", "list sessions", "sessions", "show sessions"):
        return _voice_list(sessions)

    # Default: send as prompt
    if not target:
        return VoiceCommandResponse(
            success=False,
            spoken_response="No active session found. Please start a session first.",
        )
    ok = await tmux.send_text(target, body.text.strip())
    session_label = _short_name(target)
    if ok:
        return VoiceCommandResponse(
            success=True,
            spoken_response=f"Prompt sent to {session_label}.",
            action_taken="prompt_sent",
            session_name=target,
        )
    return VoiceCommandResponse(
        success=False,
        spoken_response=f"Failed to send prompt to {session_label}.",
    )


# ---------------------------------------------------------------------------
# Android Auto specific
# ---------------------------------------------------------------------------

@router.get("/android-auto/sessions")
async def android_auto_sessions(
    registry=Depends(_registry),
    config=Depends(_config),
    _token: str = Depends(verify_token),
):
    """Session list formatted for Android Auto template constraints.

    Android Auto uses a template-based UI with strict limits:
    - List template: max 6 items
    - Each item: title (max 2 lines), icon, action
    """
    sessions = await registry.list_sessions()
    limit = min(config.vehicle_session_limit, 6)  # Android Auto max
    items = []
    for s in sessions[:limit]:
        icon = _state_icon(s.state)
        title = _short_name(s.name)
        subtitle = _STATE_LABELS.get(s.state, "Unknown")
        if s.needs_attention:
            subtitle = f"⚠ {_ATTENTION_REASONS.get(s.state, subtitle)}"
        items.append({
            "title": title,
            "subtitle": subtitle,
            "icon": icon,
            "session_name": s.name,
            "needs_attention": s.needs_attention,
        })
    return {"items": items, "total": len(sessions)}


# ---------------------------------------------------------------------------
# CarPlay specific
# ---------------------------------------------------------------------------

@router.get("/carplay/sessions")
async def carplay_sessions(
    registry=Depends(_registry),
    config=Depends(_config),
    _token: str = Depends(verify_token),
):
    """Session list formatted for CarPlay template constraints.

    CarPlay uses CPListTemplate with similar constraints:
    - Max items varies by context (typically 12)
    - Each item: text, detail text, image
    - Supports assistant cells for Siri integration
    """
    sessions = await registry.list_sessions()
    limit = min(config.vehicle_session_limit, 12)  # CarPlay allows more
    items = []
    for s in sessions[:limit]:
        text = _short_name(s.name)
        detail = _STATE_LABELS.get(s.state, "Unknown")
        if s.needs_attention:
            detail = f"Action needed: {_ATTENTION_REASONS.get(s.state, detail)}"
        items.append({
            "text": text,
            "detail_text": detail,
            "image_name": _state_image(s.state),
            "session_name": s.name,
            "needs_attention": s.needs_attention,
            "state": s.state.value,
        })
    return {"items": items, "total": len(sessions)}


@router.post("/carplay/siri-shortcut")
async def carplay_siri_shortcut(
    body: VoiceCommandRequest,
    registry=Depends(_registry),
    tmux=Depends(_tmux),
    _token: str = Depends(verify_token),
):
    """Handle Siri Shortcuts integration for CarPlay.

    Siri Shortcuts can invoke this endpoint with voice-transcribed text.
    Returns a response suitable for Siri to speak back.
    """
    body.source = "carplay"
    return await handle_voice_command(
        body=body,
        registry=registry,
        tmux=tmux,
        _token=_token,
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _short_name(name: str) -> str:
    """Extract a short display name from a session name."""
    # konsolai-Default-a1b2c3d4 -> Default-a1b2
    parts = name.replace("konsolai-", "").split("-")
    if len(parts) >= 2:
        profile = parts[0]
        sid = parts[-1][:4]
        return f"{profile}-{sid}"
    return name[:20]


def _state_icon(state: ClaudeState) -> str:
    """Map state to an icon name for Android Auto."""
    return {
        ClaudeState.NOT_RUNNING: "ic_stop",
        ClaudeState.STARTING: "ic_hourglass",
        ClaudeState.IDLE: "ic_check_circle",
        ClaudeState.WORKING: "ic_sync",
        ClaudeState.WAITING_INPUT: "ic_warning",
        ClaudeState.ERROR: "ic_error",
    }.get(state, "ic_help")


def _state_image(state: ClaudeState) -> str:
    """Map state to an image asset name for CarPlay."""
    return {
        ClaudeState.NOT_RUNNING: "session_stopped",
        ClaudeState.STARTING: "session_starting",
        ClaudeState.IDLE: "session_idle",
        ClaudeState.WORKING: "session_working",
        ClaudeState.WAITING_INPUT: "session_attention",
        ClaudeState.ERROR: "session_error",
    }.get(state, "session_unknown")


def _build_summary(total_active: int, needing_attention: int) -> str:
    """Build a TTS-friendly summary string."""
    if total_active == 0:
        return "No active sessions."
    parts = [f"{total_active} active session{'s' if total_active != 1 else ''}"]
    if needing_attention:
        parts.append(f"{needing_attention} need{'s' if needing_attention == 1 else ''} attention")
    return ". ".join(parts) + "."


async def _voice_approve(target, tmux, registry):
    if not target:
        return VoiceCommandResponse(
            success=False,
            spoken_response="No session is waiting for permission.",
        )
    session = await registry.get_session(target)
    if session and session.state != ClaudeState.WAITING_INPUT:
        return VoiceCommandResponse(
            success=False,
            spoken_response=f"{_short_name(target)} is not waiting for permission.",
            session_name=target,
        )
    ok = await tmux.send_keys(target, "Enter")
    label = _short_name(target)
    return VoiceCommandResponse(
        success=ok,
        spoken_response=f"Approved for {label}." if ok else f"Failed to approve {label}.",
        action_taken="approved" if ok else "failed",
        session_name=target,
    )


async def _voice_deny(target, tmux, registry):
    if not target:
        return VoiceCommandResponse(
            success=False,
            spoken_response="No session is waiting for permission.",
        )
    ok = await tmux.send_keys(target, "Escape")
    if ok:
        ok = await tmux.send_text(target, "n")
    label = _short_name(target)
    return VoiceCommandResponse(
        success=ok,
        spoken_response=f"Denied for {label}." if ok else f"Failed to deny {label}.",
        action_taken="denied" if ok else "failed",
        session_name=target,
    )


async def _voice_stop(target, tmux):
    if not target:
        return VoiceCommandResponse(
            success=False,
            spoken_response="No active session to stop.",
        )
    ok = await tmux.send_ctrl_c(target)
    label = _short_name(target)
    return VoiceCommandResponse(
        success=ok,
        spoken_response=f"Stopped {label}." if ok else f"Failed to stop {label}.",
        action_taken="stopped" if ok else "failed",
        session_name=target,
    )


def _voice_status(sessions):
    active = [s for s in sessions if s.state != ClaudeState.NOT_RUNNING]
    if not active:
        return VoiceCommandResponse(
            success=True,
            spoken_response="No active sessions.",
            action_taken="status",
        )
    waiting = [s for s in active if s.state == ClaudeState.WAITING_INPUT]
    working = [s for s in active if s.state == ClaudeState.WORKING]
    parts = [f"{len(active)} active session{'s' if len(active) != 1 else ''}"]
    if working:
        parts.append(f"{len(working)} working")
    if waiting:
        parts.append(f"{len(waiting)} waiting for input")
    return VoiceCommandResponse(
        success=True,
        spoken_response=". ".join(parts) + ".",
        action_taken="status",
    )


def _voice_list(sessions):
    active = [s for s in sessions if s.state != ClaudeState.NOT_RUNNING]
    if not active:
        return VoiceCommandResponse(
            success=True,
            spoken_response="No active sessions.",
            action_taken="list",
        )
    # Build list incrementally to stay within TTS limit (500 chars)
    prefix = "Active sessions: "
    names: list[str] = []
    for s in active[:5]:
        names.append(_short_name(s.name))
        candidate = prefix + ", ".join(names) + "."
        if len(candidate) > 480:
            names.pop()
            break
    text = prefix + ", ".join(names) + "."
    remaining = len(active) - len(names)
    if remaining > 0:
        text += f" And {remaining} more."
    return VoiceCommandResponse(
        success=True,
        spoken_response=text,
        action_taken="list",
    )
