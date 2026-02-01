"""Tests for Pydantic models."""

from __future__ import annotations

from konsolai_bridge.models import (
    ClaudeState,
    SessionSummary,
    TokenUsage,
    VehicleDashboard,
    VehicleSessionCard,
    VoiceCommandRequest,
    VoiceCommandResponse,
)


def test_token_usage_total():
    tu = TokenUsage(input_tokens=1000, output_tokens=500, cache_read_tokens=200, cache_creation_tokens=100)
    assert tu.total_tokens == 1800


def test_token_usage_cost():
    tu = TokenUsage(input_tokens=1_000_000, output_tokens=100_000)
    # input: 1M * 3.0/M = 3.0, output: 100K * 15.0/M = 1.5
    cost = tu.estimated_cost_usd
    assert abs(cost - 4.5) < 0.01


def test_session_summary():
    s = SessionSummary(
        name="konsolai-Default-a1b2c3d4",
        session_id="a1b2c3d4",
        profile="Default",
        state=ClaudeState.IDLE,
    )
    assert s.needs_attention is False


def test_vehicle_session_card():
    card = VehicleSessionCard(
        name="Default-a1b2",
        state=ClaudeState.WAITING_INPUT,
        state_label="Needs Input",
        needs_attention=True,
        attention_reason="Permission needed",
    )
    assert card.needs_attention is True
    assert len(card.name) <= 30


def test_voice_command_request():
    req = VoiceCommandRequest(text="approve", source="carplay")
    assert req.source == "carplay"


def test_voice_command_response():
    resp = VoiceCommandResponse(
        success=True,
        spoken_response="Approved for Default session.",
        action_taken="approved",
    )
    assert resp.success is True


def test_vehicle_dashboard():
    dash = VehicleDashboard(
        sessions=[],
        total_active=0,
        total_needing_attention=0,
        summary_text="No active sessions.",
    )
    assert len(dash.summary_text) <= 100
