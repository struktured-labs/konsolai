"""Tmux session management — mirrors TmuxManager.cpp from the C++ side."""

from __future__ import annotations

import asyncio
import logging
import re
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)

# Konsolai session name pattern: konsolai-{profile}-{8hex}
_SESSION_RE = re.compile(r"^konsolai-(.+)-([0-9a-f]{8})$")


@dataclass
class TmuxSessionInfo:
    name: str
    profile: str = ""
    session_id: str = ""
    width: int = 0
    height: int = 0
    created: int = 0  # unix timestamp
    attached: bool = False

    def __post_init__(self) -> None:
        m = _SESSION_RE.match(self.name)
        if m:
            self.profile = m.group(1)
            self.session_id = m.group(2)


async def _run(cmd: list[str], timeout: float = 5.0) -> tuple[int, str]:
    """Run a subprocess and return (returncode, stdout)."""
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=timeout)
    except asyncio.TimeoutError:
        proc.kill()
        return -1, ""
    return proc.returncode, stdout.decode(errors="replace")


class TmuxManager:
    """Async wrapper around tmux CLI, mirroring the C++ TmuxManager."""

    async def list_sessions(self) -> list[TmuxSessionInfo]:
        """List all Konsolai tmux sessions."""
        rc, out = await _run([
            "tmux", "list-sessions",
            "-F", "#{session_name}\t#{session_width}\t#{session_height}\t#{session_created}\t#{session_attached}",
        ])
        if rc != 0:
            return []
        sessions: list[TmuxSessionInfo] = []
        for line in out.strip().splitlines():
            parts = line.split("\t")
            if len(parts) < 5:
                continue
            name = parts[0]
            if not _SESSION_RE.match(name):
                continue
            info = TmuxSessionInfo(
                name=name,
                width=int(parts[1] or 0),
                height=int(parts[2] or 0),
                created=int(parts[3] or 0),
                attached=parts[4] != "0",
            )
            sessions.append(info)
        return sessions

    async def session_exists(self, name: str) -> bool:
        rc, _ = await _run(["tmux", "has-session", "-t", name])
        return rc == 0

    async def capture_pane(self, name: str, lines: int = 200) -> str:
        """Capture terminal output from a tmux pane."""
        rc, out = await _run([
            "tmux", "capture-pane", "-t", name, "-p",
            "-S", str(-lines),
        ], timeout=5.0)
        return out if rc == 0 else ""

    async def send_keys(self, name: str, keys: str) -> bool:
        """Send keystrokes to a tmux session."""
        rc, _ = await _run(["tmux", "send-keys", "-t", name, keys])
        return rc == 0

    async def send_text(self, name: str, text: str) -> bool:
        """Send text followed by Enter to a tmux session."""
        # Use send-keys with literal flag to handle special characters
        rc, _ = await _run(["tmux", "send-keys", "-t", name, "-l", text])
        if rc != 0:
            return False
        rc, _ = await _run(["tmux", "send-keys", "-t", name, "Enter"])
        return rc == 0

    async def send_ctrl_c(self, name: str) -> bool:
        """Send Ctrl+C to stop Claude."""
        rc, _ = await _run(["tmux", "send-keys", "-t", name, "C-c"])
        return rc == 0

    async def kill_session(self, name: str) -> bool:
        """Kill a tmux session."""
        rc, _ = await _run(["tmux", "kill-session", "-t", name])
        return rc == 0

    async def create_session(
        self,
        name: str,
        working_dir: str = "",
        command: str = "",
    ) -> bool:
        """Create a new tmux session."""
        cmd = ["tmux", "new-session", "-d", "-s", name]
        if working_dir:
            cmd += ["-c", working_dir]
        if command:
            cmd.append(command)
        rc, _ = await _run(cmd)
        return rc == 0
