# Konsolai Project Rules

## Build
- Always do a full rebuild (`ninja -j4` in `build/`) and confirm zero errors before considering any task complete.
- Build directory: `build/`
- Build system: CMake + Ninja

## Testing
- **MANDATORY GATE**: Run the lightweight test suite (`/test-light`) after EVERY change. No exceptions. Do not present work as complete until all tests pass.
- **MANDATORY**: Every new feature or bug fix MUST include both:
  1. **C++ unit tests** (`src/autotests/`) — logic, data structures, provider parsing, signal verification
  2. **GUI tests** (`Testing/gui-smoke-test.py` or `Testing/gui-interaction-test.py`) — AT-SPI widget presence, tree node rendering, context menu items, panel visibility
- A feature is NOT complete until both test types exist and pass. The isolated test runner (`Testing/run-isolated-gui-tests.sh`) must remain green.
- If a bug was found without a test, add a test that reproduces it BEFORE fixing it.
- **Light suite** (run frequently): `ctest --test-dir build/ --output-on-failure -R "Claude|Tmux|Token|Budget|SessionManager|SessionObserver|Agent|Notification|ProfileClaude|Resource|Prompt|OneShot|Keyboard|TabIndicator|StatusWidget"` — 27 tests, ~18s
- **Full suite** (run before releases): `ctest --test-dir build/ --output-on-failure` — includes upstream Konsole tests
- **GUI smoke tests** (run against live instance): `bash Testing/run-all-gui-tests.sh` — AT-SPI headless validation via MCP backend
- **GUI introspection via MCP**: Use the `konsolai-gui` MCP tools (`widget_tree`, `find_widget`, `click`, `read_text`, `widget_state`, `screenshot`, etc.) to validate UI changes against a live Konsolai instance. These tools use AT-SPI (not Squish) and are the primary way to verify widget presence, tree structure, and interactive behavior. Always prefer MCP tools over manual verification.
- Claude integration tests: `src/autotests/Claude*.cpp`, `src/autotests/T*.cpp`, `src/autotests/Agent*.cpp`
- GUI tests: `Testing/gui-smoke-test.py`, `Testing/gui-interaction-test.py`, `Testing/gui-lifecycle-test.py`
- GUI test backend: `tools/gui-mcp/` — AT-SPI MCP server (`atspi_backend.py`). Requires `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1` on the target app.
- Tests use QTest framework (`QTEST_GUILESS_MAIN` for non-GUI, `QTEST_MAIN` for widget tests).
- Test libraries: `Qt::Test Qt::Network konsolai_claude konsoleprivate` (add `Qt::Widgets` for widget tests)

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

## Agent Panel
- **AgentProvider** — abstract interface in `AgentProvider.h` with versioned API (`interfaceVersion()`)
- **AgentFleetProvider** — reads goals from `{fleetPath}/goals/*.yaml`, state from `~/.config/agent-fleet/`
- **AgentManagerPanel** — tree widget with context menu, registered in sidebar as `[Sessions][Agents]` tabs
- Sidebar uses `QTabWidget` (`_sidebarTabs`) wrapping both `SessionManagerPanel` and `AgentManagerPanel`
- Provider path auto-detected from `~/projects/agent-fleet` or configured via `KonsolaiSettings::agentFleetPath()`
- Tests: `AgentFleetProviderTest` (parsing, CRUD, signals), `AgentManagerPanelTest` (tree, context menu, footer)
- Docs: `doc/konsolai/agents.md`, `doc/konsolai/implementing-providers.md`

## Key Architecture
- **ClaudeSession** extends `Konsole::Session` (IS-A, not wrapper)
- **SessionManagerPanel** + **AgentManagerPanel** in tabbed sidebar dock widget
- **BudgetController** enforces per-session cost/time/token limits
- **ClaudeHookHandler** processes hook events via Unix domain socket
- **KonsolaiSettings** — singleton config via KSharedConfig (`~/.config/konsolai/konsolairc`)

## Debugging
- **Always debug for the user** — don't ask them to share logs or output. Find it yourself.
- **Logs**: Check `journalctl --user --since "10 minutes ago" | grep konsolai` for runtime logs.
- **Remote issues**: SSH to the remote host directly to verify environment (PATH, tool availability, tmux sessions).
- **Installed binary verification**: Use `strings` on the installed `.so` to confirm your changes are deployed (e.g., `strings /usr/lib/x86_64-linux-gnu/libkonsoleprivate.so | grep <unique_string>`).
- **Installation**: User runs `./install.sh` to deploy after build. Always remind them to install after a successful build.
