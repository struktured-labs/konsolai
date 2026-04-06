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

## Prerequisites

### Build Dependencies

- **CMake** 3.16+ and **Ninja** (build system)
- **Qt 6.5+** (Core, Widgets, Multimedia, PrintSupport, Concurrent, DBus)
- **KDE Frameworks 6** (KConfig, KI18n, KIO, KNotifications, KParts, KPty, KCrash, KNewStuff, KXmlGui, and more)
- **Extra CMake Modules** (ECM)
- **ICU** 61.0+ (Unicode support)
- C++17 compiler (GCC 10+ or Clang 13+)

### Runtime Dependencies

- **tmux** -- session persistence backend
- **claude** CLI -- Claude Code by Anthropic (`npm install -g @anthropic-ai/claude-code`)

### Install Packages

**Ubuntu / Debian (24.04+):**
```bash
sudo apt install cmake ninja-build g++ extra-cmake-modules \
  qt6-base-dev qt6-multimedia-dev libgl-dev \
  libkf6config-dev libkf6i18n-dev libkf6kio-dev libkf6notifications-dev \
  libkf6parts-dev libkf6pty-dev libkf6crash-dev libkf6newstuff-dev \
  libkf6xmlgui-dev libkf6bookmarks-dev libkf6coreaddons-dev \
  libkf6guiaddons-dev libkf6iconthemes-dev libkf6notifyconfig-dev \
  libkf6service-dev libkf6textwidgets-dev libkf6widgetsaddons-dev \
  libkf6windowsystem-dev libkf6configwidgets-dev libkf6dbusaddons-dev \
  libkf6globalaccel-dev libicu-dev tmux
```

**Fedora 40+:**
```bash
sudo dnf install cmake ninja-build gcc-c++ extra-cmake-modules \
  qt6-qtbase-devel qt6-qtmultimedia-devel \
  kf6-kconfig-devel kf6-ki18n-devel kf6-kio-devel kf6-knotifications-devel \
  kf6-kparts-devel kf6-kpty-devel kf6-kcrash-devel kf6-knewstuff-devel \
  kf6-kxmlgui-devel kf6-kbookmarks-devel kf6-kcoreaddons-devel \
  kf6-kguiaddons-devel kf6-kiconthemes-devel kf6-knotifyconfig-devel \
  kf6-kservice-devel kf6-ktextwidgets-devel kf6-kwidgetsaddons-devel \
  kf6-kwindowsystem-devel kf6-kconfigwidgets-devel kf6-kdbusaddons-devel \
  kf6-kglobalaccel-devel libicu-devel tmux
```

**Arch Linux:**
```bash
sudo pacman -S cmake ninja extra-cmake-modules \
  qt6-base qt6-multimedia \
  kconfig ki18n kio knotifications kparts kpty kcrash knewstuff \
  kxmlgui kbookmarks kcoreaddons kguiaddons kiconthemes knotifyconfig \
  kservice ktextwidgets kwidgetsaddons kwindowsystem kconfigwidgets \
  kdbusaddons kglobalaccel icu tmux
```

## Building

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
