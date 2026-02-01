"""WebSocket endpoint — real-time event streaming to clients."""

from __future__ import annotations

import asyncio
import json
import logging
from datetime import datetime

from fastapi import APIRouter, Query, WebSocket, WebSocketDisconnect

from ..models import WSEvent, WSEventType

logger = logging.getLogger(__name__)

router = APIRouter(tags=["websocket"])


class ConnectionManager:
    """Manages active WebSocket connections and broadcasts events."""

    def __init__(self) -> None:
        self._connections: list[WebSocket] = []
        self._lock = asyncio.Lock()

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        async with self._lock:
            self._connections.append(ws)
        logger.info("WebSocket client connected (total: %d)", len(self._connections))

    async def disconnect(self, ws: WebSocket) -> None:
        async with self._lock:
            if ws in self._connections:
                self._connections.remove(ws)
        logger.info("WebSocket client disconnected (total: %d)", len(self._connections))

    async def broadcast(self, event: WSEvent) -> None:
        """Send an event to all connected clients."""
        payload = event.model_dump_json()
        dead: list[WebSocket] = []
        async with self._lock:
            for ws in self._connections:
                try:
                    await ws.send_text(payload)
                except Exception:
                    dead.append(ws)
            for ws in dead:
                self._connections.remove(ws)

    @property
    def client_count(self) -> int:
        return len(self._connections)


# Singleton manager — attached to app.state in main.py
ws_manager = ConnectionManager()


@router.websocket("/api/ws")
async def websocket_endpoint(
    websocket: WebSocket,
    token: str = Query(default=""),
):
    """WebSocket endpoint for real-time events.

    Clients connect and receive JSON events for all sessions.
    Authentication is via the `token` query parameter.
    """
    # Validate token
    config = websocket.app.state.config
    if config.bearer_token and token != config.bearer_token:
        await websocket.close(code=4001, reason="Unauthorized")
        return

    await ws_manager.connect(websocket)
    try:
        # Keep connection alive — listen for pings/client messages
        while True:
            data = await websocket.receive_text()
            # Client can send "ping" for keepalive
            if data.strip().lower() == "ping":
                await websocket.send_text(json.dumps({"type": "pong"}))
    except WebSocketDisconnect:
        pass
    except Exception:
        logger.debug("WebSocket error", exc_info=True)
    finally:
        await ws_manager.disconnect(websocket)


async def emit_event(
    event_type: WSEventType,
    session_name: str,
    data: dict | None = None,
) -> None:
    """Helper to broadcast an event to all WebSocket clients."""
    event = WSEvent(
        type=event_type,
        session_name=session_name,
        timestamp=datetime.utcnow(),
        data=data or {},
    )
    await ws_manager.broadcast(event)
