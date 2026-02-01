# SSH Remote Sessions — Implementation Plan

## Executive Summary

Konsolai currently manages Claude Code sessions via local tmux and Unix domain sockets for hook events. This plan adds "Remote SSH Sessions" — the ability to connect to a remote machine over SSH, run Claude Code there, and manage the remote tmux session from the Konsolai GUI just like a local session.

---

## Current Architecture (What Exists Today)

**Session lifecycle (local):**

1. `MainWindow::newTab()` finds a Claude-enabled profile and opens `ClaudeSessionWizard`.
2. Wizard returns a working directory. `MainWindow::createSession()` calls `ViewManager::createSession()`, which creates a `ClaudeSession` (via `SessionManager`, which checks `Profile::ClaudeEnabled`).
3. `ClaudeSession::run()` validates tmux and claude CLI availability, starts a `ClaudeHookHandler` (Unix socket at `~/.local/share/konsolai/sessions/{id}.sock`), writes hooks config into the project's `.claude/settings.local.json`, then runs `sh -c "tmux new-session -A -s konsolai-{profile}-{id} -c {workdir} -- claude"`.
4. The `konsolai-hook-handler` binary is invoked by Claude hooks. It reads JSON from stdin, connects to the local Unix socket, and sends events back to Konsolai. For yolo mode, it checks a `.yolo` sidecar file and can auto-approve by writing approval JSON to stdout.
5. `ClaudeSessionRegistry` tracks sessions by name, persists state to `~/.local/share/konsolai/sessions.json`, and periodically polls tmux to detect orphans.

**Key local-only assumptions currently baked in:**
- `TmuxManager::executeCommand()` runs `tmux` as a local `QProcess`.
- `ClaudeHookHandler` listens on a local Unix domain socket (`QLocalServer`).
- `ClaudeProcess::isAvailable()` checks local PATH for the `claude` binary.
- `ClaudeSession::run()` validates local directory existence with `QDir(workDir).exists()`.
- The hook handler binary path is resolved from the local filesystem.
- Hooks config is written to the local filesystem at `{workdir}/.claude/settings.local.json`.

**Existing SSH infrastructure in Konsole (inherited):**
- `SSHProcessInfo` (in `src/SSHProcessInfo.h`) — lightweight parser for SSH process args (user, host, port, command).
- `SSHManager` plugin (in `src/plugins/SSHManager/`) — a sidebar dock widget that reads `~/.ssh/config`, displays SSH hosts, and sends `ssh user@host` to the active session. It manages `SSHConfigurationData` (name, host, port, sshKey, username, profileName, useSshConfig).
- `MainWindow::createSSHSession()` — creates a plain `Session` (not `ClaudeSession`) and sends an `ssh` command as text.
- `Session::hostnameChanged` signal and `RemoteTabTitle` format — Konsole already detects SSH hostname changes and can show remote host info in tab titles.

---

## Architecture Design

The central challenge is: **Claude Code must run on the remote machine** because it needs filesystem access to the remote project. This means tmux, claude CLI, and the hook handler must all be on the remote host. The local Konsolai must control the remote tmux session and somehow receive hook events.

**Chosen approach: SSH tunnel for tmux control + reverse SSH port forwarding (or SSH-piped socket) for hook events.**

```
LOCAL (Konsolai)                          REMOTE (SSH Host)
+------------------+                      +------------------+
|                  |    SSH connection     |                  |
| ClaudeSession    | ===================> | tmux session     |
|   (RemoteSSH     |    (terminal I/O)    |   running claude |
|    subclass)     |                      |                  |
|                  |                      | konsolai-hook-   |
| HookHandler      | <== SSH tunnel ===  |   handler binary |
|   (LocalServer)  |    (reverse port     |   (connects to   |
|                  |     forward)         |    tunneled sock)|
+------------------+                      +------------------+
```

**Why Claude Code must be on the remote:**
- Claude Code needs to read/write the project filesystem.
- Claude Code's MCP tools operate on local files.
- Piping Claude through SSH stdin/stdout would break the interactive TUI.

**Why the hook handler must also be on the remote:**
- Claude hooks invoke the handler binary as a subprocess of Claude Code, which runs on the remote.
- The handler reads hook event JSON from stdin and needs to send it back to Konsolai.

**Hook event relay strategy:**
- Konsolai listens on a local TCP port (instead of/in addition to the Unix socket).
- An SSH reverse tunnel maps `remote:localhost:PORT` back to `local:localhost:PORT`.
- The remote `konsolai-hook-handler` connects to `localhost:PORT` on the remote side (which tunnels to Konsolai).
- Alternatively, use `ssh -R /tmp/konsolai-{id}.sock:LOCAL_SOCKET_PATH` for Unix socket forwarding (OpenSSH 6.7+).

---

## Phase 1 (MVP): Remote tmux attachment via SSH

**Goal:** Run `ssh -t user@host tmux new-session -A -s konsolai-{name} -- claude` in the terminal. Basic remote Claude Code with session persistence. No hook events, no yolo mode.

### Changes

1. **New struct: `RemoteSSHConfig`** (in `src/claude/RemoteSSHConfig.h`)
   - host, username, port, sshKeyPath, useSshConfig, sshConfigHost, agentForwarding, remoteWorkDir, remoteTmuxPath, remoteClaudePath

2. **Extend `ClaudeSessionState`** — add `isRemote`, `remoteHost`, `remoteUser`, `remotePort` fields

3. **New subclass: `RemoteClaudeSession`** (extends `ClaudeSession`)
   - Overrides `shellCommand()` to produce SSH-wrapped tmux command
   - Overrides `run()` to skip local tmux/claude availability checks
   - `isRemote()` returns true

4. **Extend `TmuxManager`** — add remote command methods (SSH-wrapped tmux calls)

5. **Extend `ClaudeSessionWizard`** — "Local" / "Remote (SSH)" radio buttons, SSH config fields, "Test Connection" button

6. **Tab title** — remote sessions show `projectname@host`

---

## Phase 2: Remote Hook Events via SSH Tunnel

**Goal:** Hook events flowing from remote Claude Code back to local Konsolai for state tracking, notifications, and yolo mode.

### Changes

1. **TCP-based HookHandler mode** — `QTcpServer` on `127.0.0.1:PORT` instead of `QLocalServer`

2. **SSH reverse tunnel** — `ssh -R 127.0.0.1:REMOTE_PORT:127.0.0.1:LOCAL_PORT`

3. **Remote hook handler deployment** — shell script fallback for MVP, native binary for polish

4. **Remote hooks config injection** — write `.claude/settings.local.json` on remote via SSH

5. **Remote yolo mode** — yolo state file on remote

---

## Phase 3: Remote Session Registry and Orphan Detection

- Poll remote tmux sessions via SSH
- Persist `RemoteSSHConfig` in `sessions.json` for reattach across restarts
- Cache remote session lists (60s polling interval)

---

## Phase 4: Authentication and Connection Management

- SSH agent forwarding (default)
- Connection health monitoring with auto-reconnect
- SSH multiplexing via `ControlMaster`/`ControlPath`/`ControlPersist`
- Full `SSHManager` plugin integration for host discovery

---

## Phase 5: UX Polish

- Network icon in tab titles for remote sessions
- Session panel grouped by host
- Status widget shows `[host]` prefix
- Notifications include hostname
- Quick Connect dialog with recent remote hosts

---

## File Changes Summary

**New files:**
- `src/claude/RemoteSSHConfig.h`
- `src/claude/RemoteClaudeSession.h/.cpp`
- `src/claude/tools/konsolai-hook-handler-remote.sh`

**Modified files:**
- `src/claude/ClaudeSessionWizard.h/.cpp`
- `src/claude/ClaudeSessionState.h/.cpp`
- `src/claude/ClaudeSessionRegistry.h/.cpp`
- `src/claude/TmuxManager.h/.cpp`
- `src/claude/ClaudeHookHandler.h/.cpp`
- `src/claude/ClaudeMenu.h/.cpp`
- `src/claude/SessionManagerPanel.h/.cpp`
- `src/claude/KonsolaiSettings.h/.cpp`
- `src/MainWindow.cpp`
- `src/claude/CMakeLists.txt`

---

## Self-Hosting Considerations

See [design-notes.md](design-notes.md) — "Hook Handler Robustness" section for critical notes on using Konsolai to develop itself.
