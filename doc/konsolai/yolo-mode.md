# Yolo Mode

Konsolai's three-tier auto-approval system lets Claude work autonomously with configurable levels of automation.

## Level 1: Yolo Mode (Auto-Approve Permissions)

When Claude requests permission to use a tool (Bash, Write, etc.), Konsolai automatically approves it.

**How it works:**
1. **Primary path (hooks):** `konsolai-hook-handler` checks the `.yolo` file and outputs approval JSON. ClaudeProcess receives `yolo_approved: true` and emits `yoloApprovalOccurred`.
2. **Fallback path (polling):** Every 300ms, captures the terminal bottom 5 lines. `detectPermissionPrompt()` checks for `❯` with `Yes` or `Allow` on the same line, then navigates to "Always allow".

**Toggle:** Claude menu > Yolo Mode, or via the session panel context menu.

## Level 2: Double Yolo (Auto-Accept Suggestions)

When Claude finishes responding and becomes idle, Konsolai automatically accepts inline suggestions (Tab + Enter).

**Trigger:** Claude becomes Idle (finished responding)
**Action:** Tab (accept inline suggestion) + Enter (submit)
**Fires first** when `trySuggestionsFirst` is true (default)

## Level 3: Triple Yolo (Auto-Continue)

When Claude is idle and no suggestion was accepted, Konsolai sends an auto-continue prompt.

**Trigger:** Claude becomes Idle and stays idle for 2 seconds
**Action:** Sends the auto-continue prompt text + Enter
**Default prompt:** "Continue improving, debugging, fixing, adding features, or introducing tests where applicable."

## Detection Patterns

- **Permission prompt:** Line contains `❯` AND (`Yes` OR `Allow`) — checked per-line to avoid cross-line false positives
- **Idle prompt:** Last non-empty line starts with `>` or ends with `> ` — excludes lines containing `Allow`/`Deny`/`Yes`+`❯`

## Safety Controls

- **Cooldown:** 2-second cooldown between approvals prevents double-approve
- **Budget limits:** Cost, time, and token ceilings can halt approval
- **Per-session:** Each yolo level is toggleable per-session
- **File-based:** `.yolo` file presence controls hook-based approval

## Common Issues

- **Stale hooks:** Sessions clean hooks from `.claude/settings.local.json` on destroy
- **Zombie sockets:** Hook handler exits 0 on connection failure (never blocks Claude CLI)
- **ANSI codes:** Terminal capture includes escape sequences — use `contains()` not exact match
- **Pattern drift:** Claude CLI updates may change the permission/idle UI text
