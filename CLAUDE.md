# Konsolai Project Rules

## Tools
- Use `uv run python` instead of `python3` for any Python scripting needs.

## Build
- Always do a full rebuild (`ninja -j4` in `build/`) and confirm zero errors before considering any task complete.
- Build directory: `build/`
- Build system: CMake + Ninja

## Testing
- Run `ctest --test-dir build/ --output-on-failure` after changes to Claude integration code.
- Claude integration tests are in `src/autotests/Claude*.cpp` and `src/autotests/T*.cpp` (TmuxManager, TokenUsage, etc.)
- Tests use QTest framework (`QTEST_GUILESS_MAIN` for non-GUI, `QTEST_MAIN` for widget tests).
- Test libraries: `Qt::Test Qt::Network konsolai_claude konsoleprivate`

## Yolo Mode (Auto-Approval System)
Konsolai has a three-level yolo system. Each level must work independently and in combination.

### Level 1: Yolo Mode — Auto-approve permission prompts
- **Trigger**: Claude Code shows a permission prompt (tool use approval)
- **Primary path** (hook-based): `konsolai-hook-handler` checks `.yolo` file, outputs approval JSON → `ClaudeProcess` receives `yolo_approved: true` → emits `yoloApprovalOccurred` → session counts approval
- **Fallback path** (polling): Every 300ms captures terminal bottom 5 lines → `detectPermissionPrompt()` checks for `❯` with `Yes` or `Allow` on the same line → sends Down+Enter to select "Always allow"
- **Both paths must work**: hooks are primary but can fail (stale sockets, handler crash); polling is fallback
- **Critical**: Hook handler must ALWAYS exit 0 — non-zero exit breaks the entire Claude CLI session

### Level 2: Double Yolo — Auto-accept suggestions
- **Trigger**: Claude becomes Idle (finished responding)
- **Action**: Tab (accept inline suggestion) + Enter (submit)
- **Fires first** when `trySuggestionsFirst` is true (default)

### Level 3: Triple Yolo — Auto-continue with prompt
- **Trigger**: Claude becomes Idle and stays idle
- **Action**: Sends auto-continue prompt text + Enter
- **Fires as fallback** 2s after double yolo if Claude is still idle, or directly if double yolo is off
- **Idle detection**: Hook-based `Stop` event (primary) + polling every 2s checking for `>` prompt (fallback)

### Detection Patterns
- Permission prompt: line contains `❯` AND (`Yes` OR `Allow`) — checked per-line to avoid cross-line false positives
- Idle prompt: last non-empty line starts with `>` or ends with `> ` — excludes lines containing `Allow`/`Deny`/`Yes`+`❯`

### Common Pitfalls
- Stale hooks: sessions must clean hooks from `.claude/settings.local.json` on destroy (destructor calls `removeHooksFromProjectSettings`)
- Zombie sockets: socket files without listeners — hook handler must exit 0 on connection failure
- Pattern drift: Claude Code CLI updates may change the permission/idle UI text — keep detection patterns up to date
- ANSI codes: terminal capture includes escape sequences — use `contains()` not exact match
