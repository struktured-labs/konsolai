"""Konsolai Bridge — FastAPI application entry point."""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI

from .config import BridgeConfig
from .hook_listener import HookListener
from .models import ClaudeState, WSEventType
from .routes import sessions, setup, vehicle, websocket
from .session_registry import SessionRegistry
from .tmux import TmuxManager

logger = logging.getLogger("konsolai_bridge")


async def _hook_event_handler(
    session_id: str,
    event: dict[str, Any],
) -> None:
    """Process a hook event from a Claude session.

    Maps hook events to state changes and broadcasts via WebSocket.
    """
    event_type = event.get("type", "")
    registry: SessionRegistry = _app.state.registry

    if event_type == "Stop":
        registry.update_state(f"konsolai-*-{session_id}", ClaudeState.IDLE)
        await websocket.emit_event(
            WSEventType.STATE_CHANGED,
            session_id,
            {"state": "idle"},
        )

    elif event_type == "PreToolUse":
        tool = event.get("tool", "")
        # Find session name from ID
        await websocket.emit_event(
            WSEventType.STATE_CHANGED,
            session_id,
            {"state": "working", "tool": tool},
        )

    elif event_type == "PostToolUse":
        tool = event.get("tool", "")
        await websocket.emit_event(
            WSEventType.STATE_CHANGED,
            session_id,
            {"state": "working", "tool": tool, "phase": "post"},
        )

    elif event_type == "Notification":
        message = event.get("message", "")
        await websocket.emit_event(
            WSEventType.NOTIFICATION,
            session_id,
            {"message": message},
        )

    # Generic forwarding for any event type
    await websocket.emit_event(
        WSEventType.NOTIFICATION,
        session_id,
        {"hook_event": event_type, "raw": event},
    )


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan — start/stop background services."""
    config: BridgeConfig = app.state.config
    tmux = TmuxManager()
    registry = SessionRegistry(config.sessions_file, config.socket_dir, tmux)
    hook_listener = HookListener(config.socket_dir, _hook_event_handler)

    app.state.tmux = tmux
    app.state.registry = registry
    app.state.hook_listener = hook_listener

    await hook_listener.start()
    logger.info(
        "Konsolai Bridge started on %s:%d",
        config.host,
        config.port,
    )
    yield
    await hook_listener.stop()
    logger.info("Konsolai Bridge stopped")


def create_app(config: BridgeConfig | None = None) -> FastAPI:
    """Create and configure the FastAPI application."""
    if config is None:
        config = BridgeConfig.load()

    app = FastAPI(
        title="Konsolai Bridge",
        description="REST/WebSocket bridge for remote Konsolai control — Android Auto & CarPlay",
        version="0.1.0",
        lifespan=lifespan,
    )
    app.state.config = config

    # Mount route modules
    app.include_router(sessions.router)
    app.include_router(websocket.router)
    app.include_router(vehicle.router)
    app.include_router(setup.router)

    return app


# Module-level app for hook handler reference
_app: FastAPI = None  # type: ignore[assignment]


def cli_main() -> None:
    """CLI entry point — parse args and run the server."""
    parser = argparse.ArgumentParser(description="Konsolai Bridge Service")
    parser.add_argument("--host", default=None, help="Bind address")
    parser.add_argument("--port", type=int, default=None, help="Bind port")
    parser.add_argument("--config", type=Path, default=None, help="Config file path")
    parser.add_argument("--log-level", default="INFO", help="Log level")
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )

    config = BridgeConfig.load(args.config)
    if args.host:
        config.host = args.host
    if args.port:
        config.port = args.port

    global _app
    _app = create_app(config)

    import uvicorn
    uvicorn.run(
        _app,
        host=config.host,
        port=config.port,
        log_level=args.log_level.lower(),
    )


if __name__ == "__main__":
    cli_main()
