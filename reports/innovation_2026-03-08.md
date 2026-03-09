# Konsolai Innovation Report — 2026-03-08

## Review Summary

Reviewed last 20 commits, existing reports, and all uncommitted working changes.
Found three complete, well-tested features staged but uncommitted. Verified build and tests pass.

---

## Status: Uncommitted Features Ready to Ship

Three significant improvements are implemented in the working tree and passing all tests:

### 1. Auto-archive Closed Sessions (`SessionManagerPanel`)
**Files:** `src/claude/SessionManagerPanel.cpp/.h`

Old closed sessions accumulate in the session list indefinitely.
Now sessions in the "Closed" state with `lastAccessed` > 7 days are automatically archived
on a 5-minute background timer (with an immediate pass at startup after 10s).

Rules:
- Only targets `isExpired && !isArchived && !isDismissed && !isPinned`
- Never touches sessions with active `ClaudeSession` objects
- Timer is paused/resumed alongside other background timers (window visibility)

Tests added:
- `testAutoArchiveClosedSessions` — 8-day-old closed session gets archived
- `testAutoArchiveSkipsPinned` — pinned sessions are never touched
- `testAutoArchiveSkipsRecent` — 3-day-old sessions stay in Closed

### 2. QFileSystemWatcher for Near-Instant Token Updates (`ClaudeSession`)
**Files:** `src/claude/ClaudeSession.cpp/.h`

Token usage previously polled every 30 seconds. Now:
- A `QFileSystemWatcher` watches the Claude project directory for JSONL changes
- A 500ms debounce timer prevents excessive refreshes during streaming
- The 30s poll timer is kept as fallback for edge cases (file moves, new conversations)
- Directory is re-watched if `m_workingDir` changes between sessions

This gives sub-second token display updates when Claude writes to its JSONL file.

No new unit tests for the watcher itself (ClaudeSession is integration-level);
covered implicitly by the existing `testTokenRefreshTimerStarted` pattern.
**TODO:** Add a test that writes to a temp project dir and verifies debounced refresh fires.

### 3. Staggered Session Reattach (`MainWindow`)
**Files:** `src/MainWindow.cpp`

When Konsolai starts with multiple orphaned sessions, all `run()` calls fired simultaneously,
causing UI jank and potential tmux race conditions.

Fix: session objects are created and registered first (Phase 1), then `run()` is called
with 50ms spacing via `QTimer::singleShot(i * 50, ...)` (Phase 2).

---

## Build & Test Results

```
ninja -C build/ -j4          → no errors, no warnings
ctest (23 Claude unit tests)  → 100% pass (23/23)
SessionManagerPanelTest (66)  → 100% pass (QT_QPA_PLATFORM=offscreen)
```

---

## Remaining Work (Not Implemented This Run)

### A. Test for file watcher debounce (HIGH VALUE, LOW RISK)
Add to `TokenTrackingTest`:
```cpp
void testFileWatcherTriggersRefresh()
```
- Create ClaudeSession with a temp workDir
- Call startTokenTracking()
- Write a new JSONL line to the project dir
- Wait up to 1s (QTRY_COMPARE) for tokenUsage to update
- Confirms the watcher + debounce path works end-to-end

### B. Categorized logging (MEDIUM — from 2026-03-05 report)
145 `qDebug()` calls in `src/claude/` should become `qCDebug(KonsolaiDebug, ...)`.
Enables runtime filtering via `QT_LOGGING_RULES=konsolai.claude.debug=true`.
Requires: define `Q_LOGGING_CATEGORY` in a shared header, mechanical replacement.

### C. Commit the staged features
All three features above are ready:
```
git add -p  # or specific files
git commit -m "Add auto-archive, file watcher tokens, staggered reattach"
```
