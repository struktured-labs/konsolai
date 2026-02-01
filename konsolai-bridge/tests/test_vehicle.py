"""Tests for vehicle integration endpoints (Android Auto + CarPlay)."""

from __future__ import annotations


def test_vehicle_dashboard(client):
    resp = client.get("/api/vehicle/dashboard")
    assert resp.status_code == 200
    data = resp.json()
    assert "sessions" in data
    assert data["total_active"] >= 0
    assert "summary_text" in data


def test_vehicle_dashboard_session_limit(client, app):
    app.state.config.vehicle_session_limit = 1
    resp = client.get("/api/vehicle/dashboard")
    data = resp.json()
    assert len(data["sessions"]) <= 1


def test_vehicle_dashboard_attention_count(client):
    resp = client.get("/api/vehicle/dashboard")
    data = resp.json()
    # Dev session is WAITING_INPUT
    assert data["total_needing_attention"] >= 1


def test_android_auto_sessions(client):
    resp = client.get("/api/vehicle/android-auto/sessions")
    assert resp.status_code == 200
    data = resp.json()
    assert "items" in data
    for item in data["items"]:
        assert "title" in item
        assert "subtitle" in item
        assert "icon" in item
        assert "session_name" in item


def test_android_auto_max_six_items(client, app):
    app.state.config.vehicle_session_limit = 10
    resp = client.get("/api/vehicle/android-auto/sessions")
    data = resp.json()
    # Android Auto limit is 6 items
    assert len(data["items"]) <= 6


def test_carplay_sessions(client):
    resp = client.get("/api/vehicle/carplay/sessions")
    assert resp.status_code == 200
    data = resp.json()
    assert "items" in data
    for item in data["items"]:
        assert "text" in item
        assert "detail_text" in item
        assert "image_name" in item
        assert "state" in item


def test_voice_command_approve(client):
    resp = client.post("/api/vehicle/voice", json={
        "text": "approve",
        "source": "android_auto",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True
    assert "approved" in data["spoken_response"].lower() or "approved" in data.get("action_taken", "")


def test_voice_command_deny(client):
    resp = client.post("/api/vehicle/voice", json={
        "text": "deny",
        "source": "android_auto",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True


def test_voice_command_stop(client):
    resp = client.post("/api/vehicle/voice", json={
        "text": "stop",
        "source": "android_auto",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True


def test_voice_command_status(client):
    resp = client.post("/api/vehicle/voice", json={
        "text": "status",
        "source": "android_auto",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True
    assert "session" in data["spoken_response"].lower() or "active" in data["spoken_response"].lower()


def test_voice_command_list(client):
    resp = client.post("/api/vehicle/voice", json={
        "text": "list sessions",
        "source": "android_auto",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True


def test_voice_command_prompt(client):
    resp = client.post("/api/vehicle/voice", json={
        "text": "fix the login bug",
        "source": "android_auto",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True
    assert data["action_taken"] == "prompt_sent"


def test_carplay_siri_shortcut(client):
    resp = client.post("/api/vehicle/carplay/siri-shortcut", json={
        "text": "approve",
        "source": "carplay",
    })
    assert resp.status_code == 200
    data = resp.json()
    assert data["success"] is True
