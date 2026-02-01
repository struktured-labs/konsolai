# Android Companion App — Implementation Plan

## Executive Summary

The Konsolai Android companion app provides voice-first, phone-ergonomic remote control of Claude Code sessions running on a Konsolai host. Rather than reimplementing Konsolai, it is a thin client that talks to a lightweight REST/WebSocket bridge service running alongside Konsolai on the host machine, which in turn interacts with existing tmux sessions, D-Bus interfaces, and the Claude hook system.

---

## Architecture

```
+------------------+        HTTPS/WSS         +-------------------+
|  Android Client  | <======================> |  Konsolai Bridge   |
|  (Kotlin/Compose)|     (SSH tunnel or       |  (Python/FastAPI)  |
|                  |      direct LAN)         |                    |
+------------------+                          +-------------------+
        |                                            |    |    |
    [Voice STT]                                      |    |    |
    [Touch UI]                                       v    v    v
    [Notifications]                             tmux    D-Bus   Hook
                                                CLI     API     Sockets
```

Three layers:
1. **Konsolai Bridge Service** — Python FastAPI daemon on host, exposes REST + WebSocket
2. **Transport** — SSH port-forward, Tailscale VPN, or direct LAN with TLS
3. **Android Client** — Kotlin + Jetpack Compose, voice-first

---

## Bridge Service API

### REST Endpoints

```
GET  /api/sessions                           — List all sessions
GET  /api/sessions/{name}                    — Session detail
GET  /api/sessions/{name}/transcript         — Parsed transcript
POST /api/sessions/{name}/prompt             — Send prompt
POST /api/sessions/{name}/approve            — Approve permission
POST /api/sessions/{name}/deny               — Deny permission
POST /api/sessions/{name}/stop               — Send Ctrl+C
POST /api/sessions/{name}/kill               — Kill session
PUT  /api/sessions/{name}/yolo               — Set yolo modes
GET  /api/sessions/{name}/token-usage        — Token metrics (see design-notes.md)
POST /api/sessions/new                       — Create new session
```

### WebSocket

```
WS /api/ws — Real-time event stream
```

Events: `state_changed`, `permission_requested`, `task_started`, `task_finished`, `notification`, `transcript_update`, `token_usage_updated`

### Authentication

Bearer token in `~/.config/konsolai/bridge.conf`. QR code setup via `/setup/qr`.

---

## Android Client Design

### Technology: Kotlin + Jetpack Compose

- Native Android `SpeechRecognizer` for best STT quality
- FCM for push notifications
- Material Design 3 for phone ergonomics

### Session List Screen

- Material Cards (min 72dp height) with color-coded status dots
- Sessions needing attention sorted to top with inline APPROVE/DENY
- Swipe right: archive/pin. Swipe left: kill.
- Pull-to-refresh

### Chat View Screen (Primary Interaction)

- Chat-like message bubbles (user right, Claude left, system centered)
- Collapsible tool usage cards
- **Permission banners**: sticky bottom with 56dp+ APPROVE (green) / DENY (red) buttons
- Quick actions overflow: stop, restart, yolo toggles, raw terminal view

### Voice Input System

**Speech-to-Text:**
- Primary: Android `SpeechRecognizer` (free, offline-capable)
- Fallback: Whisper API (better accuracy for technical terms)

**Hold-to-Talk (Default):**
1. Press+hold MIC button
2. Live partial transcription in text field
3. Release → final text for review
4. Tap SEND or say "send it"

**Voice Commands (Phase 3):**
- "approve" / "yes" → `approvePermission()`
- "deny" / "no" → `denyPermission()`
- "stop" → Ctrl+C
- "yolo mode on/off" → toggle

**Text-to-Speech:**
- Summarized responses (first 500 chars)
- Tool usage collapsed to "[tool] on [file]"
- Queue-based, tap to skip

### Notifications

| Event | Priority | Actions |
|-------|----------|---------|
| Permission requested | HIGH (sound) | Approve, Deny |
| Task complete | DEFAULT (silent) | Open Session |
| Waiting for input | HIGH (vibrate) | Open Session |
| Error | HIGH (sound) | Open Session |

Direct action from notification shade via `NotificationCompat.Action`.

---

## Transport and Security

1. **SSH Port Forward** (most secure): `ssh -L 8472:localhost:8472 user@host`
2. **Tailscale/WireGuard** (most convenient): VPN + bearer token
3. **Direct LAN + TLS** (self-signed, TOFU model)

---

## Phased Implementation

### Phase 1: SSH-Only MVP

- Termux script (`konsolai-remote.sh`): list sessions, send prompts, approve/deny via SSH
- Basic Kotlin app: SSH connection, session list, send prompt, approve/deny, voice input

### Phase 2: Bridge Service + Structured API

- Python FastAPI bridge with REST + WebSocket
- Android app: replace SSH with API calls, chat view, foreground service for notifications
- Token usage tracking endpoint (see design-notes.md)

### Phase 3: Voice + Polish

- Full voice system (hold-to-talk, voice commands, TTS)
- FCM via ntfy.sh relay
- Swipe gestures, haptic feedback, dark/light theme
- Token budget dashboard

### Phase 4: Advanced

- Conversation history browser
- Multi-host support
- Offline mode (queue prompts)
- Wear OS companion
- Home screen widget
- Tablet layout

---

## Termux Fallback

```bash
# konsolai-remote
# Commands: l (list), s N (select), p TEXT (prompt), v (voice),
#           a (approve), d (deny), x (stop), t (transcript),
#           y 1|2|3 (yolo toggle), q (quit)
```

---

## Token Tracking Integration

See [design-notes.md](design-notes.md) — "Token Budget Tracking" section. The Android app is a natural place to surface token metrics since it provides a dashboard view of all sessions.

---

## Bridge Service Structure

```
konsolai-bridge/
  pyproject.toml
  konsolai_bridge/
    main.py
    config.py
    auth.py
    tmux.py               # mirrors TmuxManager.cpp
    session_registry.py   # reads ~/.local/share/konsolai/sessions.json
    hook_listener.py      # connects to hook sockets
    transcript_parser.py  # ANSI stripping + conversation extraction
    token_tracker.py      # per-session token usage aggregation
    routes/
      sessions.py
      websocket.py
      setup.py
    models.py
  systemd/
    konsolai-bridge.service
```
