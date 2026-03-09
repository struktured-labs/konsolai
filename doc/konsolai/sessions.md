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
