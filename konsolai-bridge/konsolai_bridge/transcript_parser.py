"""Transcript parser — strips ANSI codes and extracts conversation structure.

Parses tmux capture-pane output to extract user/assistant message turns.
"""

from __future__ import annotations

import re
from typing import Optional

from .models import Transcript, TranscriptMessage

# ANSI escape sequence pattern
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]|\x1b\].*?\x07|\x1b\(B")

# Claude Code prompt patterns
_USER_PROMPT_RE = re.compile(r"^(?:❯|>|\$)\s*(.+)")
_ASSISTANT_START_RE = re.compile(r"^(?:Claude|⏎)\s*")
_TOOL_USE_RE = re.compile(r"^\s*(?:Read|Edit|Write|Bash|Glob|Grep|Task|WebFetch|WebSearch|NotebookEdit|TodoWrite)\s*")
_SEPARATOR_RE = re.compile(r"^[-─━═]{3,}")


def strip_ansi(text: str) -> str:
    """Remove ANSI escape sequences from text."""
    return _ANSI_RE.sub("", text)


def parse_transcript(raw: str, session_name: str) -> Transcript:
    """Parse raw terminal output into structured conversation messages."""
    clean = strip_ansi(raw)
    lines = clean.splitlines()
    messages: list[TranscriptMessage] = []
    current_role: Optional[str] = None
    current_lines: list[str] = []

    def flush() -> None:
        nonlocal current_role, current_lines
        if current_role and current_lines:
            content = "\n".join(current_lines).strip()
            if content:
                messages.append(TranscriptMessage(
                    role=current_role,
                    content=content,
                ))
        current_role = None
        current_lines = []

    for line in lines:
        stripped = line.strip()
        if not stripped:
            if current_lines:
                current_lines.append("")
            continue

        # Check for user prompt
        m = _USER_PROMPT_RE.match(stripped)
        if m:
            flush()
            current_role = "user"
            current_lines = [m.group(1)]
            continue

        # Check for separator (between turns)
        if _SEPARATOR_RE.match(stripped):
            flush()
            continue

        # Check for tool use
        if _TOOL_USE_RE.match(stripped):
            if current_role != "assistant":
                flush()
                current_role = "assistant"
                current_lines = []
            current_lines.append(stripped)
            continue

        # Non-prompt line after a user prompt → this is an assistant response
        if current_role == "user":
            flush()
            current_role = "assistant"
            current_lines = [stripped]
            continue

        # Default: append to current role (or start assistant)
        if current_role is None:
            current_role = "assistant"
            current_lines = []
        current_lines.append(stripped)

    flush()
    return Transcript(
        session_name=session_name,
        messages=messages,
        raw=raw,
    )
