"""Setup endpoints — QR code pairing and health check."""

from __future__ import annotations

import base64
import io
import json

from fastapi import APIRouter, Depends, Request
from fastapi.responses import HTMLResponse

from ..auth import verify_token

router = APIRouter(prefix="/api/setup", tags=["setup"])


@router.get("/health")
async def health():
    """Health check — no auth required."""
    return {"status": "ok", "service": "konsolai-bridge"}


@router.get("/info")
async def bridge_info(
    request: Request,
    _token: str = Depends(verify_token),
):
    """Return bridge configuration info."""
    config = request.app.state.config
    return {
        "host": config.host,
        "port": config.port,
        "vehicle_session_limit": config.vehicle_session_limit,
        "tts_max_chars": config.tts_max_chars,
    }


@router.get("/qr", response_class=HTMLResponse)
async def setup_qr(request: Request):
    """Generate a QR code for Android/CarPlay app pairing.

    The QR code encodes a JSON payload with the connection info
    and bearer token so the mobile app can be configured with
    a single scan.
    """
    config = request.app.state.config
    payload = json.dumps({
        "host": config.host,
        "port": config.port,
        "token": config.bearer_token,
        "version": 1,
    })

    # Try to generate a real QR code, fall back to text
    try:
        import qrcode  # type: ignore[import-untyped]
        qr = qrcode.make(payload)
        buf = io.BytesIO()
        qr.save(buf, format="PNG")
        img_b64 = base64.b64encode(buf.getvalue()).decode()
        img_tag = f'<img src="data:image/png;base64,{img_b64}" width="300" height="300">'
    except ImportError:
        # No qrcode library — show raw payload
        img_tag = f'<pre style="font-size:14px;background:#f0f0f0;padding:16px;border-radius:8px">{payload}</pre>'

    return f"""<!DOCTYPE html>
<html>
<head><title>Konsolai Bridge Setup</title></head>
<body style="font-family:sans-serif;text-align:center;padding:40px">
    <h1>Konsolai Bridge</h1>
    <p>Scan this QR code with the Konsolai Android or CarPlay app:</p>
    {img_tag}
    <p style="color:#666;font-size:12px;margin-top:24px">
        Connection: {config.host}:{config.port}
    </p>
</body>
</html>"""
