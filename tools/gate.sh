#!/bin/bash
# Konsolai light-suite gate.
#
# WHY THIS EXISTS: `ctest ... | grep "tests passed" && git commit` commits on a
# red suite. A pipeline's exit status is its LAST command's, so grep's 0 masks
# ctest's 8. The verdict must be captured before any shaping and must be the
# only thing that decides. Measured 2026-07-29: ctest exits 8 on failure and
# the piped form exits 0.
#
# Flaky tests are RETRIED ONCE and reported as RETRIED, never as PASSED. A
# suite that silently forgives an intermittent failure is a suite that cannot
# tell a flake from a regression.
set -uo pipefail
BUILD="${1:-build}"
LOGDIR="$(cd "$(dirname "$0")/.." && pwd)/tmp"
mkdir -p "$LOGDIR"
LOG="$LOGDIR/konsolai-gate.log"

# HookCleanupRace/RemoteSshArgs/SessionLinkFilter/YoloPolling were konsolai tests
# that no prefix here matched, so the MANDATORY gate skipped them -- including
# yolo polling and the hook-cleanup race. Found 2026-07-29 by running this gate
# against the full suite for the first time. They add 0.31s.
DEFAULT_LIGHT='Claude|Tmux|Token|Budget|SessionManager|SessionObserver|Agent|Notification|ProfileClaude|Resource|Prompt|OneShot|Keyboard|TabIndicator|StatusWidget|Letta|TreeToolbar|Konsolai|Merge|Broadcast|SessionTreeWidget|Assistant|Reorganize|Codex|Wizard|HookCleanupRace|RemoteSshArgs|SessionLinkFilter|YoloPolling'
# Overridable ONLY so the gate can be tested against itself (see tools/gate-selftest.sh).
LIGHT="${KONSOLAI_GATE_FILTER:-$DEFAULT_LIGHT}"

# BUILD FIRST. ctest never compiles: run it after editing source and it happily
# reports on the PREVIOUS build. Verified 2026-07-29 -- with a source file newer
# than its binary the gate still returned a verdict, about code that was not on
# disk. A green from stale binaries is the same defect as a green from someone
# else's test list: consulted, and answering about a tree that isn't ours.
echo "GATE: building ($BUILD)..."
if ! ninja -C "$BUILD" -j4 >"$LOGDIR/konsolai-build.log" 2>&1; then
    echo "GATE: FAILED — build errors. Tests would have run against stale binaries."
    grep -E "error:|FAILED:" "$LOGDIR/konsolai-build.log" | head -10
    exit 1
fi

# SCOPE, NOT JUST VERDICT. `$?` answers "did what ran pass"; it is silent about
# whether the right thing ran. Measured 2026-07-29, all exit 0:
#   filter matching zero tests · filter typo'd down to a subset · unconfigured
#   build dir. Each one made this gate print PASSED having verified nothing.
# So the count is checked against the tree, and against a floor that only rises.
# Numbers parsed from tool output must be PROVEN numeric before use. `[ x -eq 0 ]`
# on a non-numeric value exits 2, and `if` reads 2 as false -- so the guard falls
# through to the pass path. It would fail open exactly when it is needed, since a
# change in ctest's output format is the drift this check exists to catch.
# ${VAR:-0} covers empty and nothing else.
require_num() { # value, description
    case "$1" in
    '' | *[!0-9]*)
        echo "GATE: FAILED — could not parse $2 (got '$1'). ctest output format"
        echo "      may have changed; refusing rather than guessing. Log: $LOG"
        exit 1
        ;;
    esac
}

SELECTED=$(ctest --test-dir "$BUILD" -N -R "$LIGHT" 2>/dev/null | sed -n 's/^Total Tests: //p' | tail -1)
SELECTED=${SELECTED:-0}
require_num "$SELECTED" "the selected-test count"

ctest --test-dir "$BUILD" -j1 -R "$LIGHT" >"$LOG" 2>&1
EC=$?
grep -E "tests passed" "$LOG" | head -2

# "N% tests passed, X tests failed out of RAN"
RAN=$(sed -n 's/.*tests failed out of \([0-9]*\).*/\1/p' "$LOG" | tail -1)
RAN=${RAN:-0}
require_num "$RAN" "the ran-test count"

if [ "$RAN" -ne "$SELECTED" ] || [ "$SELECTED" -eq 0 ]; then
    echo "GATE: FAILED — scope check. selected=$SELECTED ran=$RAN"
    echo "      A suite that did not run cannot have passed. Check the build dir"
    echo "      is configured and the filter still matches. Log: $LOG"
    exit 1
fi

# Monotonic floor, DERIVED not hand-listed: it records what the tree actually
# had and refuses a drop. Adding tests raises it automatically, so it cannot go
# stale the way a literal `EXPECT=41` would. Skipped when the filter is
# overridden, since a self-test's count is not the suite's count.
FLOOR_FILE="$(dirname "$0")/gate-floor.txt"
if [ -z "${KONSOLAI_GATE_FILTER:-}" ]; then
    # Read raw and refuse on corruption. Sanitising (tr -dc '0-9') would turn
    # "4x1" into a plausible 41 and hide the damage.
    FLOOR=$(cat "$FLOOR_FILE" 2>/dev/null || echo 0)
    FLOOR=${FLOOR:-0}
    require_num "$FLOOR" "the floor in $FLOOR_FILE"
    if [ "$SELECTED" -lt "$FLOOR" ]; then
        echo "GATE: FAILED — the light suite SHRANK: $FLOOR -> $SELECTED tests."
        echo "      Tests vanished from the tree or the filter stopped matching."
        echo "      If deliberate, lower $FLOOR_FILE in the same commit."
        exit 1
    fi
    [ "$SELECTED" -gt "$FLOOR" ] && echo "$SELECTED" >"$FLOOR_FILE" \
        && echo "GATE: floor raised $FLOOR -> $SELECTED"
fi

# MEMBERSHIP, NOT JUST COUNT. selected-vs-ran compares two numbers that both
# come from ctest, so a test file that was never registered is invisible to it:
# 65 files on disk and 65 registered tests agreed here while two files were
# unregistered. CLAUDE.md requires every feature to ship C++ tests, and a test
# that is never built is one the gate reports PASSED without running.
#
# Ratcheted against a recorded baseline rather than asserted empty, because two
# files have been unregistered since the fork (677d94305, inherited from
# upstream Konsole -- their names have never appeared in CMakeLists.txt in any
# commit). The set may not GROW; shrinking it is the fix.
KNOWN_FILE="$(dirname "$0")/gate-unregistered.txt"
REGISTERED=$(ctest --test-dir "$BUILD" -N 2>/dev/null | sed -n 's/^ *Test *#[0-9]*: //p' | sort -u)
# Absolute, so running the gate from another directory cannot make the glob
# expand to nothing and read as clean.
AUTOTESTS="$(cd "$(dirname "$0")/.." && pwd)/src/autotests"
if [ ! -d "$AUTOTESTS" ]; then
    echo "GATE: FAILED — cannot find $AUTOTESTS to check test registration."
    exit 1
fi
UNREG=$(for f in "$AUTOTESTS"/*Test.cpp; do
    n=$(basename "$f" .cpp)
    echo "$REGISTERED" | grep -qx "$n" || echo "$n"
done | sort -u)
# NOT comm. `comm` requires both inputs sorted in the collation it compares with,
# and when it reads a process substitution it cannot seek, so it silently returns
# a SHORT list instead of erroring. Measured 2026-07-29 on a 1137-line list:
#   real file        1137   correct
#   <(same bytes)     905   silently short
# A short list here means an unregistered test file goes unreported -- the gate
# would pass over exactly what it exists to catch. grep -Fxv has no sortedness
# precondition at all, so there is nothing to get wrong.
NEW=$(printf '%s\n' "$UNREG" | grep -Fxv -f "$KNOWN_FILE" 2>/dev/null || true)
NEW=$(printf '%s' "$NEW" | sed '/^$/d')
if [ -n "$NEW" ]; then
    echo "GATE: FAILED — test file(s) on disk that ctest never runs:"
    echo "$NEW" | sed 's/^/        /'
    echo "      Add them to src/autotests/CMakeLists.txt. A test that is not"
    echo "      registered cannot fail, and this gate would call that PASSED."
    exit 1
fi

# COVERAGE, NOT JUST MEMBERSHIP.
#
# selected-vs-ran are two numbers that both come from the same ctest+filter, so a
# filter matching too LITTLE makes both of them small, they agree, and this gate
# goes green. That is not a hypothetical: .claude/commands/test-light.md carried
# its own copy of the filter, drifted 14 terms behind, and was running 31 tests
# where this gate runs 45 -- and its scope check would have reported 31 == 31.
# Two instruments sharing a blind spot are one instrument wearing a disguise.
#
# So check a property the filter cannot influence: which test sources konsolai
# authored, derived from git history rather than from a list anyone maintains.
# Every konsolai-authored *Test.cpp must be selected by the light filter. The
# fork SHA is a pin, but a commit SHA is immutable and the fork point never
# moves -- unlike a line number or a hand-listed name.
if [ -z "${KONSOLAI_GATE_FILTER:-}" ]; then
    FORK=677d94305
    REPO="$(cd "$(dirname "$0")/.." && pwd)"
    git -C "$REPO" log "$FORK"..HEAD --diff-filter=A --name-only --format= \
        -- 'src/autotests/*Test.cpp' 2>/dev/null | sort -u | sed 's|.*/||;s|\.cpp$||' >"$LOGDIR/konsolai-tests.txt"
    OURS=$(wc -l <"$LOGDIR/konsolai-tests.txt")
    # A derivation that enumerates nothing looks exactly like full coverage.
    if [ "$OURS" -lt 20 ]; then
        echo "GATE: FAILED — only $OURS konsolai-authored tests derived from git."
        echo "      Expected dozens. The fork SHA $FORK may be missing from this"
        echo "      clone, or this is not the konsolai repo. Refusing to treat an"
        echo "      empty derivation as full coverage."
        exit 1
    fi
    ctest --test-dir "$BUILD" -N -R "$LIGHT" 2>/dev/null \
        | sed -n 's/^ *Test *#[0-9]*: //p' | sort -u >"$LOGDIR/selected-names.txt"
    UNCOVERED=$(grep -Fxv -f "$LOGDIR/selected-names.txt" "$LOGDIR/konsolai-tests.txt" || true)
    if [ -n "$UNCOVERED" ]; then
        echo "GATE: FAILED — konsolai-authored test(s) the light filter does not select:"
        echo "$UNCOVERED" | sed 's/^/        /'
        echo "      Registering a test in CMake is not enough; DEFAULT_LIGHT must"
        echo "      match its name. These would build, pass, and never run."
        exit 1
    fi
fi

if [ "$EC" -eq 0 ]; then
    echo "GATE: PASSED (ctest exit 0, $RAN/$SELECTED tests ran)"
    exit 0
fi

echo "GATE: first pass failed (ctest exit $EC) — retrying failures only:"
grep -E "\(Failed\)" "$LOG" | head -10

# Retry the names from THIS run's log. `--rerun-failed` reads a persisted
# LastTestsFailed.log, so when the first pass fails without recording failures
# it silently reruns some EARLIER run's list — and a pass there would be
# reported as RETRIED, i.e. green, on a scope that was never ours.
FAILED=$(sed -n 's/.*[0-9]* - \([A-Za-z0-9_]*\) (Failed.*/\1/p' "$LOG" | sort -u)
if [ -z "$FAILED" ]; then
    echo "GATE: FAILED — ctest exit $EC but no failing test named. Log: $LOG"
    exit 1
fi
RETRY_RE="^($(echo "$FAILED" | paste -sd'|' -))$"
ctest --test-dir "$BUILD" -j1 -R "$RETRY_RE" >"$LOG.retry" 2>&1
RC=$?
if [ "$RC" -eq 0 ]; then
    echo "GATE: RETRIED — the above failed under load and passed in isolation."
    echo "      Treat as flaky, NOT as green. Investigate before it hides a regression."
    exit 0
fi
# KNOWN PRE-EXISTING FAILURE, carved out with a deadline rather than suppressed.
#
# TokenTrackingTest::testFileWatcherTriggersRefresh fails INTERMITTENTLY in the
# full binary (roughly half of runs) and passes standalone. Verified pre-existing
# on 2026-07-29 by stashing all local changes, rebuilding, and reproducing
# identically — it is not ours.
#
# THE CAUSE IS UNKNOWN. This comment previously read "QFileSystemWatcher debounce
# vs accumulated load", which was inferred and never measured. Ruled out since:
# it is not order-dependent (the two watcher tests pass together 4/4), and it is
# not cross-run disk state (fixed 2026-07-29: the test was leaking dirs into
# ~/.claude/projects on failure, and stopping that did not change the rate).
# Do not record a mechanism here without measuring it.
#
# THIS CARVE-OUT SELF-DESTRUCTS: if TokenTrackingTest is the ONLY failure and it
# starts passing, the grep below stops matching and the gate goes green through
# the normal path. If ANY other suite fails, the carve-out does not apply and
# the gate refuses. It cannot silently widen.
# POSITIVE evidence required, not absence. An empty log — ctest failing to
# launch at all — makes both counts zero, and two empty sets agree perfectly:
# the carve-out would fire on a total failure. So it applies only when the
# carved-out suite is DEMONSTRABLY the failure, and nothing else is.
MINE=$(grep -cE "TokenTrackingTest.*\(Failed\)" "$LOG")
OTHERS=$(grep -E "\(Failed\)" "$LOG" | grep -cv "TokenTrackingTest")
if [ "$MINE" -ge 1 ] && [ "$OTHERS" -eq 0 ]; then
    echo "GATE: PASSED-WITH-KNOWN-FLAKE — only TokenTrackingTest failed."
    echo "      Pre-existing (verified against a stashed tree). Fix it and delete"
    echo "      this carve-out; it is a deadline, not a permission slip."
    exit 0
fi
echo "GATE: FAILED — still red on rerun (exit $RC). Log: $LOG.retry"
exit 1
