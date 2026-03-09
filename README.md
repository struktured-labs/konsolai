# Konsolai - Claude-Native Terminal Emulator

Konsolai is a Claude-native terminal emulator forked from KDE's Konsole, designed to provide seamless integration between Claude AI sessions and the terminal experience.

## Features

**Claude Integration**
- 1 Tab = 1 Claude Session with tmux-backed persistence
- Claude hooks integration for real-time state tracking (idle/working/waiting)
- Session wizard for project setup, git worktrees, and task configuration

**Yolo Mode (3-Level Auto-Approval)**
- Level 1: Auto-approve permission prompts (hook-based + polling fallback)
- Level 2: Auto-accept inline suggestions (Tab + Enter on idle)
- Level 3: Auto-continue with configurable prompt when idle

**Session Management**
- Sidebar panel with pinned, active, detached, archived, and discovered categories
- Automatic detection and reattachment of orphaned tmux sessions
- Remote SSH sessions with full yolo mode support
- Git worktree integration for feature branch sessions
- Quick session switcher (`Ctrl+Shift+P`)

**Agent Panel**
- Monitor persistent background agents from agent-fleet or custom providers
- Trigger runs, set briefs, add steering notes from context menu
- Interactive attachment: open a Claude tab connected to an agent's tmux session
- Aggregate daily spend tracking across all agents
- Pluggable provider architecture with versioned interface

**Budget Controls**
- Per-session cost, time, and token limits
- Per-agent per-run and daily budgets
- Weekly/monthly global spending limits
- Soft (warn) and hard (block) budget policies

**Notifications**
- System tray status indicators
- Desktop popup notifications via KNotification
- Audio alerts for important events
- In-terminal visual status indicators

**D-Bus Interface**
- Full control over Claude sessions via D-Bus
- Send prompts, approve/deny permissions
- Access session transcripts

## Building

### Dependencies
- Qt6 (Core, Gui, Widgets, Network, Multimedia, Concurrent, DBus)
- KDE Frameworks 6 (KNotifications, KPty, KConfig, KI18n, KStatusNotifierItem, KDBusAddons)
- tmux (runtime)
- claude CLI (runtime)

### Build Instructions
```bash
mkdir build && cd build
cmake ..
ninja -j4
```

### Running Tests
```bash
# Unit tests (50 tests)
ctest --test-dir build/ --output-on-failure

# GUI tests (requires running Konsolai instance)
bash Testing/run-all-gui-tests.sh
```

### Installation
```bash
./install.sh
```

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                      Konsolai Application                        │
├──────────────────────────────────────────────────────────────────┤
│  MainWindow ──► ViewManager ──► ClaudeSession (per tab)          │
│       │              │               │                           │
│       │              │         ┌─────┼─────────┐                │
│       │              │         │     │         │                │
│       ▼              ▼         ▼     ▼         ▼                │
│  Claude Menu    Tab Status   Tmux  ClaudeProcess  HookHandler   │
│                 Indicators  Manager   │           │              │
│                                │     │           │              │
│                                ▼     ▼           ▼              │
│                            tmux    State      Unix Socket       │
│                           session  Machine    (hook events)     │
│                          (persist)                              │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Sidebar (QTabWidget)                                    │    │
│  │  ┌──────────────────┐ ┌──────────────────┐              │    │
│  │  │ SessionManager   │ │ AgentManager     │              │    │
│  │  │ Panel            │ │ Panel            │              │    │
│  │  │  - Pinned        │ │  - AgentProvider │              │    │
│  │  │  - Active        │ │    (abstract)    │              │    │
│  │  │  - Detached      │ │  - AgentFleet    │              │    │
│  │  │  - Archived      │ │    Provider      │              │    │
│  │  │  - Discovered    │ │  - (future       │              │    │
│  │  │                  │ │    providers)    │              │    │
│  │  └──────────────────┘ └──────────────────┘              │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
│  BudgetController ──► Cost/Time/Token Tracking                   │
│  NotificationManager ──► Tray / Desktop / Audio / Terminal       │
│  KonsolaiSettings ──► ~/.config/konsolai/konsolairc              │
└──────────────────────────────────────────────────────────────────┘
```

## Directory Structure

| Directory          | Description                                                   |
| ------------------ | ------------------------------------------------------------- |
| `/src`             | Core terminal emulator source code                            |
| `/src/claude`      | Claude integration (sessions, hooks, yolo, agents, budgets)   |
| `/src/autotests`   | C++ unit tests (QTest framework, 50+ tests)                   |
| `/Testing`         | GUI tests (AT-SPI smoke tests, interaction tests)             |
| `/doc/konsolai`    | Konsolai-specific documentation                               |
| `/desktop`         | Desktop files for launching Konsolai                          |
| `/data`            | Color schemes, keyboard layouts, and other data files         |
| `/tools`           | Hook handler binary, GUI MCP server                           |

## Documentation

See [doc/konsolai/](doc/konsolai/README.md) for detailed guides:
- [Session Management](doc/konsolai/sessions.md)
- [Yolo Mode](doc/konsolai/yolo-mode.md)
- [Agent Panel](doc/konsolai/agents.md)
- [Implementing Providers](doc/konsolai/implementing-providers.md)
- [Budget Controls](doc/konsolai/budget-controls.md)
- [Keyboard Shortcuts](doc/konsolai/keyboard-shortcuts.md)
- [Testing Guide](doc/konsolai/testing.md)

## Credits

Konsolai is built on top of [Konsole](https://konsole.kde.org), KDE's terminal emulator. Original Konsole is (c) 1997-2022 The Konsole Developers.

Claude integration by Struktured Labs.

## License

This program is licensed under the GNU General Public License version 2.
See COPYING for details.

## Links

- [GitHub Repository](https://github.com/struktured-labs/konsolai)
- [Original Konsole](https://konsole.kde.org)
