# Konsolai - Claude-Native Terminal Emulator

Konsolai is a Claude-native terminal emulator forked from KDE's Konsole, designed to provide seamless integration between Claude AI sessions and the terminal experience.

## Features

**Claude Integration**
- 1 Tab = 1 Claude Session
- tmux-backed session persistence
- Claude hooks integration for notifications
- Session state tracking (idle/working/waiting)

**Notifications**
- System tray status indicators
- Desktop popup notifications via KNotification
- Audio alerts for important events
- In-terminal visual status indicators

**Session Management**
- Sessions persist in tmux when window closes
- Automatic detection and reattachment of orphaned sessions
- Session registry with state serialization

**D-Bus Interface**
- Full control over Claude sessions via D-Bus
- Send prompts, approve/deny permissions
- Access session transcripts

## Building

### Dependencies
- Qt6 (Core, Gui, Widgets, Network)
- KDE Frameworks 6 (KNotifications, KPty, KConfig, KI18n, etc.)
- tmux (runtime)
- claude CLI (runtime)

### Build Instructions
```bash
mkdir build && cd build
cmake ..
make
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Konsolai Application                      │
├─────────────────────────────────────────────────────────────┤
│  MainWindow ──► ViewManager ──► ClaudeSession (per tab)     │
│       │              │               │                       │
│       │              │         ┌─────┴─────┐                │
│       │              │         │           │                │
│       ▼              ▼         ▼           ▼                │
│  Claude Menu    Tab Status   TmuxManager  HookHandler       │
│                 Indicators       │           │              │
│                                  │           │              │
│                                  ▼           ▼              │
│                              tmux session  Unix Socket      │
│                              (persistent)  (hook events)    │
│                                                             │
│  NotificationManager ◄──────────────────────┘               │
│  ├─ System Tray                                             │
│  ├─ Desktop Popup (KNotification)                           │
│  ├─ Audio Alerts                                            │
│  └─ In-Terminal Visual                                      │
└─────────────────────────────────────────────────────────────┘
```

## Directory Structure

| Directory          | Description                                                   |
| ------------------ | ------------------------------------------------------------- |
| `/src`             | Core terminal emulator source code                            |
| `/src/claude`      | Claude-specific integration code                              |
| `/desktop`         | Desktop files for launching Konsolai                          |
| `/data`            | Color schemes, keyboard layouts, and other data files         |
| `/doc`             | Documentation for users and developers                        |

## Credits

Konsolai is built on top of [Konsole](https://konsole.kde.org), KDE's terminal emulator. Original Konsole is (c) 1997-2022 The Konsole Developers.

Claude integration by Struktured Labs.

## License

This program is licensed under the GNU General Public License version 2.
See COPYING for details.

## Links

- [GitHub Repository](https://github.com/struktured-labs/konsolai)
- [Original Konsole](https://konsole.kde.org)
