"""Tests for setup and health endpoints."""

from __future__ import annotations


def test_health_no_auth(unauth_client):
    """Health endpoint should work without authentication."""
    resp = unauth_client.get("/api/setup/health")
    assert resp.status_code == 200
    assert resp.json()["status"] == "ok"


def test_bridge_info(client):
    resp = client.get("/api/setup/info")
    assert resp.status_code == 200
    data = resp.json()
    assert data["host"] == "127.0.0.1"
    assert data["port"] == 8472


def test_qr_page(unauth_client):
    """QR setup page should be accessible without auth."""
    resp = unauth_client.get("/api/setup/qr")
    assert resp.status_code == 200
    assert "Konsolai Bridge" in resp.text
