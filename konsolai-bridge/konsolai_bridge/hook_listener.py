"""Hook listener — connects to Konsolai Unix sockets to receive Claude events.

Each Konsolai session creates a Unix socket at:
    ~/.konsolai/sessions/{session-id}.sock

The hook handler binary (konsolai-hook-handler) sends JSON messages to this
socket when Claude triggers hooks (Notification, Stop, PreToolUse, PostToolUse).

This module monitors the socket directory and relays events to the bridge's
WebSocket subscribers.
"""

from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path
from typing import Any, Callable, Coroutine

logger = logging.getLogger(__name__)

# Type for event callback: async fn(session_id: str, event: dict)
EventCallback = Callable[[str, dict[str, Any]], Coroutine[Any, Any, None]]


class HookListener:
    """Monitors Konsolai hook sockets and forwards events."""

    def __init__(self, socket_dir: Path, callback: EventCallback) -> None:
        self._socket_dir = socket_dir
        self._callback = callback
        self._tasks: dict[str, asyncio.Task[None]] = {}
        self._monitor_task: asyncio.Task[None] | None = None
        self._running = False

    async def start(self) -> None:
        """Begin monitoring the socket directory."""
        self._running = True
        self._socket_dir.mkdir(parents=True, exist_ok=True)
        self._monitor_task = asyncio.create_task(self._monitor_loop())
        logger.info("HookListener started — watching %s", self._socket_dir)

    async def stop(self) -> None:
        """Cancel all listener tasks and wait for them to finish."""
        self._running = False
        all_tasks = list(self._tasks.values())
        if self._monitor_task is not None:
            self._monitor_task.cancel()
            all_tasks.append(self._monitor_task)
        for task in self._tasks.values():
            task.cancel()
        if all_tasks:
            await asyncio.gather(*all_tasks, return_exceptions=True)
        self._tasks.clear()
        self._monitor_task = None

    async def _monitor_loop(self) -> None:
        """Periodically scan for new/removed sockets."""
        while self._running:
            try:
                current_sockets = {
                    p.stem: p
                    for p in self._socket_dir.glob("*.sock")
                    if p.is_socket()
                }
                # Start listeners for new sockets
                for sid, path in current_sockets.items():
                    if sid not in self._tasks or self._tasks[sid].done():
                        self._tasks[sid] = asyncio.create_task(
                            self._listen_socket(sid, path)
                        )
                # Clean up finished tasks
                dead = [
                    sid for sid, t in self._tasks.items()
                    if t.done() and sid not in current_sockets
                ]
                for sid in dead:
                    del self._tasks[sid]
            except Exception:
                logger.exception("Error in hook monitor loop")
            await asyncio.sleep(2.0)

    async def _listen_socket(self, session_id: str, path: Path) -> None:
        """Connect to a single session's hook socket and read events."""
        logger.info("Connecting to hook socket for session %s", session_id)
        while self._running:
            try:
                reader, writer = await asyncio.open_unix_connection(str(path))
                async for line in reader:
                    line_str = line.decode(errors="replace").strip()
                    if not line_str:
                        continue
                    try:
                        event = json.loads(line_str)
                        await self._callback(session_id, event)
                    except json.JSONDecodeError:
                        logger.warning(
                            "Non-JSON hook data from %s: %s",
                            session_id,
                            line_str[:200],
                        )
                # Socket closed — wait before reconnecting
                writer.close()
            except (ConnectionRefusedError, FileNotFoundError):
                pass
            except Exception:
                logger.debug("Hook socket error for %s", session_id, exc_info=True)
            if self._running:
                await asyncio.sleep(2.0)
