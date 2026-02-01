#!/usr/bin/env python3
"""One-time setup script for the Konsolai Bridge.

Creates config, generates auth token, installs systemd user service.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from .config import BridgeConfig


def setup() -> None:
    """Interactive setup for the bridge service."""
    print("=== Konsolai Bridge Setup ===\n")

    # 1. Create/load config
    config = BridgeConfig.load()
    print(f"Config file: ~/.config/konsolai/bridge.conf")
    print(f"Bearer token: {config.bearer_token[:8]}...{config.bearer_token[-4:]}")
    print(f"Bind: {config.host}:{config.port}\n")

    # 2. Install systemd user service
    systemd_dir = Path.home() / ".config" / "systemd" / "user"
    systemd_dir.mkdir(parents=True, exist_ok=True)
    service_src = Path(__file__).parent.parent / "systemd" / "konsolai-bridge.service"
    service_dst = systemd_dir / "konsolai-bridge.service"

    if service_src.exists():
        shutil.copy2(service_src, service_dst)
        print(f"Installed systemd service: {service_dst}")
        subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
        print("Run to start: systemctl --user enable --now konsolai-bridge")
    else:
        print(f"Systemd service template not found at {service_src}")

    # 3. Ensure socket directory exists
    config.socket_dir.mkdir(parents=True, exist_ok=True)

    print("\n=== Setup Complete ===")
    print(f"\nTo start manually: konsolai-bridge")
    print(f"To pair mobile app: open http://{config.host}:{config.port}/api/setup/qr")


if __name__ == "__main__":
    setup()
