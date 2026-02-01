"""Shared test fixtures for the Konsolai Bridge test suite."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from konsolai_bridge.config import BridgeConfig
from konsolai_bridge.main import create_app
from konsolai_bridge.models import ClaudeState
from konsolai_bridge.session_registry import SessionRegistry
from konsolai_bridge.tmux import TmuxManager, TmuxSessionInfo


@pytest.fixture
def config(tmp_path: Path) -> BridgeConfig:
    """Create a test config with temp paths."""
    return BridgeConfig(
        host="127.0.0.1",
        port=8472,
        bearer_token="test-token-123",
        sessions_file=tmp_path / "sessions.json",
        socket_dir=tmp_path / "sockets",
        vehicle_session_limit=5,
    )


@pytest.fixture
def mock_tmux() -> AsyncMock:
    """Mock TmuxManager with sample sessions."""
    tmux = AsyncMock(spec=TmuxManager)
    tmux.list_sessions.return_value = [
        TmuxSessionInfo(
            name="konsolai-Default-a1b2c3d4",
            width=120,
            height=40,
            created=1700000000,
            attached=True,
        ),
        TmuxSessionInfo(
            name="konsolai-Dev-e5f6a7b8",
            width=120,
            height=40,
            created=1700001000,
            attached=False,
        ),
    ]
    tmux.session_exists.return_value = True
    tmux.capture_pane.return_value = "❯ hello\nClaude response here\n❯ "
    tmux.send_keys.return_value = True
    tmux.send_text.return_value = True
    tmux.send_ctrl_c.return_value = True
    tmux.kill_session.return_value = True
    tmux.create_session.return_value = True
    return tmux


@pytest.fixture
def mock_registry(config: BridgeConfig, mock_tmux: AsyncMock) -> SessionRegistry:
    """Create a SessionRegistry with mocked tmux."""
    registry = SessionRegistry(config.sessions_file, config.socket_dir, mock_tmux)
    # Pre-populate state cache
    registry.update_state("konsolai-Default-a1b2c3d4", ClaudeState.IDLE)
    registry.update_state("konsolai-Dev-e5f6a7b8", ClaudeState.WAITING_INPUT)
    return registry


@pytest.fixture
def app(config: BridgeConfig, mock_tmux: AsyncMock, mock_registry: SessionRegistry):
    """Create a FastAPI test app with mocked dependencies."""
    app = create_app(config)
    app.state.tmux = mock_tmux
    app.state.registry = mock_registry
    return app


@pytest.fixture
def client(app) -> TestClient:
    """Create a test client with auth header."""
    return TestClient(app, headers={"Authorization": "Bearer test-token-123"})


@pytest.fixture
def unauth_client(app) -> TestClient:
    """Create a test client without auth."""
    return TestClient(app)
