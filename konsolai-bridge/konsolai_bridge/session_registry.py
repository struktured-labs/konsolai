"""Session registry — reads Konsolai session state from disk and tmux.

Mirrors ClaudeSessionRegistry from the C++ side.  Reads:
  - ~/.local/share/konsolai/sessions.json   (persisted session metadata)
  - tmux list-sessions                       (live sessions)
  - ~/.konsolai/sessions/*.sock              (hook sockets)
"""

from __future__ import annotations

import json
import logging
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from .models import (
    ClaudeState,
    SessionDetail,
    SessionSummary,
    TokenUsage,
    YoloSettings,
)
from .tmux import TmuxManager, TmuxSessionInfo

logger = logging.getLogger(__name__)


def _parse_state(raw: str) -> ClaudeState:
    """Convert a C++ state string to our enum."""
    mapping = {
        "NotRunning": ClaudeState.NOT_RUNNING,
        "Starting": ClaudeState.STARTING,
        "Idle": ClaudeState.IDLE,
        "Working": ClaudeState.WORKING,
        "WaitingInput": ClaudeState.WAITING_INPUT,
        "WaitingForInput": ClaudeState.WAITING_INPUT,
        "Error": ClaudeState.ERROR,
    }
    return mapping.get(raw, ClaudeState.NOT_RUNNING)


class SessionRegistry:
    """Aggregates session info from multiple sources."""

    def __init__(
        self,
        sessions_file: Path,
        socket_dir: Path,
        tmux: TmuxManager,
    ) -> None:
        self._sessions_file = sessions_file
        self._socket_dir = socket_dir
        self._tmux = tmux

        # In-memory state overlay (updated by hook events)
        self._state_cache: dict[str, ClaudeState] = {}

    def update_state(self, session_name: str, state: ClaudeState) -> None:
        """Update cached state for a session (called from hook events)."""
        self._state_cache[session_name] = state

    async def list_sessions(self) -> list[SessionSummary]:
        """Return summaries of all known sessions."""
        persisted = self._read_persisted()
        live = await self._tmux.list_sessions()
        live_names = {s.name for s in live}

        results: list[SessionSummary] = []
        # Merge persisted data with live tmux sessions
        seen: set[str] = set()
        for info in live:
            seen.add(info.name)
            meta = persisted.get(info.name, {})
            state = self._state_cache.get(info.name, _parse_state(meta.get("state", "")))
            needs_attention = state in (ClaudeState.WAITING_INPUT, ClaudeState.ERROR)
            results.append(SessionSummary(
                name=info.name,
                session_id=info.session_id,
                profile=info.profile,
                state=state,
                needs_attention=needs_attention,
                token_usage=self._extract_tokens(meta),
                yolo=self._extract_yolo(meta),
                created_at=datetime.fromtimestamp(info.created, tz=timezone.utc) if info.created else None,
                last_activity=self._parse_dt(meta.get("lastActivity")),
            ))
        # Include persisted sessions not currently in tmux (detached/dead)
        for name, meta in persisted.items():
            if name not in seen:
                results.append(SessionSummary(
                    name=name,
                    session_id=meta.get("sessionId", ""),
                    profile=meta.get("profile", ""),
                    state=ClaudeState.NOT_RUNNING,
                    needs_attention=False,
                    token_usage=self._extract_tokens(meta),
                    yolo=self._extract_yolo(meta),
                    created_at=self._parse_dt(meta.get("createdAt")),
                    last_activity=self._parse_dt(meta.get("lastActivity")),
                ))
        # Sort: needs-attention first, then by last activity
        results.sort(key=lambda s: (not s.needs_attention, s.last_activity or datetime.min.replace(tzinfo=timezone.utc)), reverse=False)
        return results

    async def get_session(self, name: str) -> Optional[SessionDetail]:
        """Return full detail for a single session."""
        persisted = self._read_persisted()
        meta = persisted.get(name, {})
        exists = await self._tmux.session_exists(name)
        if not exists and not meta:
            return None
        state = self._state_cache.get(name, _parse_state(meta.get("state", "")))
        if not exists:
            state = ClaudeState.NOT_RUNNING

        # Parse session name for profile/id
        profile = meta.get("profile", "")
        session_id = meta.get("sessionId", "")
        if not profile and "-" in name:
            parts = name.split("-")
            if len(parts) >= 3:
                profile = "-".join(parts[1:-1])
                session_id = parts[-1]

        return SessionDetail(
            name=name,
            session_id=session_id,
            profile=profile,
            state=state,
            needs_attention=state in (ClaudeState.WAITING_INPUT, ClaudeState.ERROR),
            token_usage=self._extract_tokens(meta),
            yolo=self._extract_yolo(meta),
            created_at=self._parse_dt(meta.get("createdAt")),
            last_activity=self._parse_dt(meta.get("lastActivity")),
            working_dir=meta.get("workingDir", ""),
            model=meta.get("model", "default"),
            auto_continue_prompt=meta.get("autoContinuePrompt", ""),
            approval_count=meta.get("approvalCount", 0),
        )

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _read_persisted(self) -> dict[str, dict]:
        """Read the sessions.json file."""
        if not self._sessions_file.exists():
            return {}
        try:
            data = json.loads(self._sessions_file.read_text())
            if isinstance(data, list):
                return {s["name"]: s for s in data if "name" in s}
            if isinstance(data, dict):
                return data
        except Exception:
            logger.warning("Failed to read sessions.json", exc_info=True)
        return {}

    @staticmethod
    def _extract_tokens(meta: dict) -> TokenUsage:
        tok = meta.get("tokenUsage", {})
        return TokenUsage(
            input_tokens=tok.get("inputTokens", 0),
            output_tokens=tok.get("outputTokens", 0),
            cache_read_tokens=tok.get("cacheReadTokens", 0),
            cache_creation_tokens=tok.get("cacheCreationTokens", 0),
        )

    @staticmethod
    def _extract_yolo(meta: dict) -> YoloSettings:
        return YoloSettings(
            yolo=meta.get("yoloMode", False),
            double_yolo=meta.get("doubleYoloMode", False),
            triple_yolo=meta.get("tripleYoloMode", False),
        )

    @staticmethod
    def _parse_dt(val: Optional[str]) -> Optional[datetime]:
        if not val:
            return None
        try:
            return datetime.fromisoformat(val)
        except (ValueError, TypeError):
            return None
