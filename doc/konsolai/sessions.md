# Session Management

## Creating Sessions

New Claude sessions can be created via:
- **New Tab** (`Ctrl+Shift+T`) — opens a new Claude session in the default project directory
- **Session Wizard** — configure profile, working directory, git worktree, task prompt
- **Reattach** — reconnect to an orphaned tmux session

## Session Lifecycle

1. **Created** — Session object created, tmux session started
2. **Running** — Claude CLI active, processing prompts
3. **Idle** — Claude waiting for input
4. **Detached** — Tab closed but tmux session persists (can reattach)
5. **Finished** — Claude CLI exited, tmux session cleaned up

## Session Panel

The left sidebar (`View > Session Manager`) shows all sessions organized by category:

- **Pinned** — Important sessions kept at the top
- **Active** — Currently running sessions with live status
- **Detached** — Tmux sessions without a Konsole tab (can reattach)
- **Closed** — Recently terminated sessions (metadata preserved)
- **Archived** — Long-term storage for reference
- **Discovered** — Orphaned tmux sessions found on startup

### Session Actions (Right-Click)

| Action | Description |
|--------|-------------|
| Focus | Switch to this session's tab |
| Attach | Reattach a detached tmux session |
| Pin / Unpin | Keep session at top of list |
| Archive | Move to archived category |
| Close | Terminate the Claude CLI |
| Dismiss | Hide from list without deleting |
| Purge | Delete all metadata |

### Category and Project-Group Actions (Right-Click)

**Restart All Live in Category (N)** / **Restart All Live in Group (N)** restarts
every live session under the node — to pick up a new CLI, MCP server or plugin
version — after one confirmation. "Live" means its tmux session is running:

- Sessions with an open tab restart in place (the pane is respawned; the tab stays).
- Live sessions with no tab are reopened in a tab first, then restarted.
- Remote sessions are skipped and the dialog says so: restart respawns through
  the local tmux server, which can't reach a session on an SSH host.

Each session resumes **its own** conversation, even when several share a
working directory. The ID comes from the running CLI's own record
(`~/.claude/sessions/<pid>.json`, matched by tmux session), then from hook
events, and only as a last resort from the newest transcript in the workdir.
Codex sessions restart as Codex, and a per-session model pick is kept.

## Session-intercom channel (DMs between sessions)

Every Claude session launches with `--channels plugin:session-intercom@struktured-labs`
so intercom DMs arrive between turns as `<channel>` tags. Two things make that work:

- **The plugin must be on the channel allowlist**, set once per machine in
  `/etc/claude-code/managed-settings.json`. Run `bash tools/enable-channel-allowlist.sh`
  (`--undo` removes it). Without it the session still starts, but the channel
  doesn't register and the startup screen says the plugin is "not on the approved
  channels allowlist". konsolai used to pass `--dangerously-load-development-channels`
  instead, which raised a blocking warning dialog on every launch and restart.
- **`MCP_PROTOCOL_NEGOTIATION=legacy`** prefixes the command. Claude Code won't
  register a channel server that negotiates MCP revision 2026-07-28, and the
  intercom server's SDK does; `claude --debug` then logs "Channel notifications
  skipped: server did not declare claude/channel capability" and no DM is pushed.

## Remote Sessions

Sessions can run on remote machines via SSH:
1. Configure SSH host, username, port in the Session Wizard
2. Konsolai creates a tmux session on the remote host
3. Claude CLI runs in the remote tmux session
4. All yolo mode features work over the SSH tunnel

## Git Worktree Integration

The Session Wizard supports creating sessions in git worktrees:
1. Select a source repository
2. Choose or create a branch
3. Konsolai creates the worktree and starts a Claude session in it
