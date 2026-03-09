# Konsolai

Konsolai extends KDE's Konsole terminal emulator with integrated AI assistant capabilities powered by Claude.

## Core Features

- **Claude Sessions** — Interactive Claude Code sessions running in tmux-backed terminal tabs
- **Session Management** — Pin, archive, reattach, and discover sessions across tmux
- **Yolo Mode** — Three-tier auto-approval system (permission prompts, suggestions, auto-continue)
- **Agent Panel** — Monitor and steer persistent background agents from agent-fleet or other providers
- **Budget Controls** — Per-session and per-agent cost, time, and token limits
- **Notifications** — Audio, desktop, system tray, and in-terminal notifications
- **Remote Sessions** — SSH-tunneled Claude sessions on remote machines

## Architecture

Konsolai adds a Claude integration layer on top of Konsole's existing session infrastructure:

- `ClaudeSession` extends `Konsole::Session` — each Claude tab IS a Konsole session
- `ClaudeProcess` manages the Claude CLI lifecycle and state machine
- `TmuxManager` provides session persistence through tmux
- `ClaudeHookHandler` processes hook events from the Claude CLI
- `SessionManagerPanel` provides the sidebar UI for session management
- `AgentManagerPanel` provides the sidebar UI for persistent agent monitoring
- `AgentProvider` abstract interface enables pluggable agent backends

## Documentation

- [Session Management](sessions.md)
- [Yolo Mode](yolo-mode.md)
- [Agent Panel](agents.md)
- [Implementing Providers](implementing-providers.md)
- [Budget Controls](budget-controls.md)
- [Keyboard Shortcuts](keyboard-shortcuts.md)
- [Testing Guide](testing.md)
