"""Tests for session REST endpoints."""

from __future__ import annotations


def test_list_sessions(client):
    resp = client.get("/api/sessions")
    assert resp.status_code == 200
    data = resp.json()
    assert len(data) == 2
    names = {s["name"] for s in data}
    assert "konsolai-Default-a1b2c3d4" in names
    assert "konsolai-Dev-e5f6a7b8" in names


def test_list_sessions_needs_attention_first(client):
    resp = client.get("/api/sessions")
    data = resp.json()
    # Dev session is WAITING_INPUT so should be sorted to top
    assert data[0]["name"] == "konsolai-Dev-e5f6a7b8"
    assert data[0]["needs_attention"] is True


def test_get_session(client):
    resp = client.get("/api/sessions/konsolai-Default-a1b2c3d4")
    assert resp.status_code == 200
    data = resp.json()
    assert data["name"] == "konsolai-Default-a1b2c3d4"
    assert data["state"] == "idle"


def test_get_session_not_found(client, mock_tmux):
    mock_tmux.session_exists.return_value = False
    resp = client.get("/api/sessions/nonexistent")
    assert resp.status_code == 404


def test_send_prompt(client):
    resp = client.post(
        "/api/sessions/konsolai-Default-a1b2c3d4/prompt",
        json={"text": "hello claude"},
    )
    assert resp.status_code == 202
    assert resp.json()["status"] == "sent"


def test_approve_permission(client):
    resp = client.post("/api/sessions/konsolai-Default-a1b2c3d4/approve")
    assert resp.status_code == 202
    assert resp.json()["status"] == "approved"


def test_deny_permission(client):
    resp = client.post("/api/sessions/konsolai-Default-a1b2c3d4/deny")
    assert resp.status_code == 202
    assert resp.json()["status"] == "denied"


def test_stop_session(client):
    resp = client.post("/api/sessions/konsolai-Default-a1b2c3d4/stop")
    assert resp.status_code == 202
    assert resp.json()["status"] == "stopped"


def test_kill_session(client):
    resp = client.post("/api/sessions/konsolai-Default-a1b2c3d4/kill")
    assert resp.status_code == 202
    assert resp.json()["status"] == "killed"


def test_get_transcript(client):
    resp = client.get("/api/sessions/konsolai-Default-a1b2c3d4/transcript")
    assert resp.status_code == 200
    data = resp.json()
    assert data["session_name"] == "konsolai-Default-a1b2c3d4"
    assert "raw" in data


def test_token_usage(client):
    resp = client.get("/api/sessions/konsolai-Default-a1b2c3d4/token-usage")
    assert resp.status_code == 200
    data = resp.json()
    assert "input_tokens" in data
    assert "estimated_cost_usd" in data


def test_auth_required(unauth_client):
    resp = unauth_client.get("/api/sessions")
    assert resp.status_code == 401
