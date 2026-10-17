#!/bin/bash
# Hermetic GUI test for "Restart All Live" on category and project-group nodes.
#
# run-isolated-gui-tests.sh isolates the display and D-Bus but NOT the data:
# its instance reads and writes the real ~/.local/share/konsolai/sessions.json
# and sees every real session as dead. This runner also sandboxes HOME, the
# XDG dirs and the tmux server, so nothing it does can reach the user's
# sessions, config, ~/.claude, or live tmux sessions.
#
# Usage: bash Testing/run-restart-live-gui-test.sh
set -u
cd "$(dirname "$0")/.."
ROOT="$PWD"
BINARY="$ROOT/build/bin/konsolai"
if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found: $BINARY (build first: ninja -C build -j4)"
    exit 1
fi

# Kept short so the tmux socket fits in sun_path (108 bytes). konsolai's hook
# sockets live deeper and do NOT fit, so hooks are dead in here — the same
# state as sessions reattached after a konsolai restart, which is the case
# restart must handle without them (it reads the CLI's own record instead).
SANDBOX="$ROOT/tmp/grl$$"
mkdir -p "$SANDBOX/home" "$SANDBOX/tmux" "$SANDBOX/bin" "$SANDBOX/projects/cowir-alpha" "$SANDBOX/projects/cowir-beta"
chmod 700 "$SANDBOX/tmux"

# Stand-in for the Claude CLI, first on PATH for konsolai and the tmux server:
# the real CLI must never launch here. Like the real one, it records which
# conversation it is on in ~/.claude/sessions/<pid>.json keyed to its tmux
# pane — the record konsolai reads to resume each session's own conversation.
cat >"$SANDBOX/bin/claude" <<'SH'
#!/bin/sh
conv="${FAKE_CONVERSATION_ID:-}"
while [ $# -gt 0 ]; do
    [ "$1" = "--resume" ] && conv="$2"
    shift
done
mkdir -p "$HOME/.claude/sessions"
printf '{"pid":%d,"sessionId":"%s","tmux":"%s:@0.%%0","updatedAt":%s000}\n' \
    $$ "$conv" "$(tmux display-message -p '#S')" "$(date +%s)" >"$HOME/.claude/sessions/$$.json"
exec sleep 3600
SH
chmod +x "$SANDBOX/bin/claude"
export PATH="$SANDBOX/bin:$PATH"

export HOME="$SANDBOX/home"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_CACHE_HOME="$HOME/.cache"
export TMUX_TMPDIR="$SANDBOX/tmux"
export KONSOLAI_WORKSPACE=guitest
unset TMUX TMUX_PANE

# Every tmux call in this script names the sandbox socket explicitly. A bare
# `tmux kill-server` with a lost TMUX_TMPDIR would kill the user's real server.
SOCK="$TMUX_TMPDIR/tmux-$(id -u)/default"
case "$SOCK" in
"$SANDBOX"/*) ;;
*)
    echo "ERROR: tmux socket $SOCK is outside the sandbox; refusing to run"
    exit 1
    ;;
esac
# tmux -S does not create the socket directory; the default-socket lookup
# konsolai uses expects it at $TMUX_TMPDIR/tmux-UID with mode 700.
mkdir -p "$(dirname "$SOCK")" && chmod 700 "$(dirname "$SOCK")"
stmux() { tmux -S "$SOCK" "$@"; }

ALPHA="$SANDBOX/projects/cowir-alpha"
BETA="$SANDBOX/projects/cowir-beta"
NOW="$(date +%Y-%m-%dT%H:%M:%S)"

# Category "cowir" (LCP of the two project names) with three sessions:
#  - a1, a2 in cowir-alpha: live at launch, so startup reattaches them into
#    tabs. They share a workdir, each on its own conversation.
#  - b1 in cowir-beta: the test starts its tmux AFTER launch, so it is live
#    with no tab. Its saved lastResumeSessionId is deliberately stale; the
#    live CLI record must win.
# Profiles load from GenericDataLocation/konsole (Konsole's dir), not konsolai/.
mkdir -p "$XDG_DATA_HOME/konsolai" "$XDG_DATA_HOME/konsole" "$XDG_CONFIG_HOME"
cat >"$XDG_DATA_HOME/konsole/Claude.profile" <<'PROFILE'
[General]
Name=Claude
Parent=FALLBACK/

[Claude Integration]
ClaudeEnabled=true
ClaudeTmuxPersistence=true
PROFILE
python3 - "$XDG_DATA_HOME/konsolai/sessions.json" "$ALPHA" "$BETA" "$NOW" <<'PY'
import json, sys
path, alpha, beta, now = sys.argv[1:]
def session(sid, workdir, **extra):
    return {"sessionId": sid, "sessionName": f"konsolai-guitest-Claude-{sid}", "profileName": "Claude",
            "workingDirectory": workdir, "isPinned": False, "isArchived": False, "isExpired": False,
            "lastAccessed": now, "createdAt": now, **extra}
json.dump([session("a1a1a1a1", alpha), session("a2a2a2a2", alpha),
           session("b1b1b1b1", beta, lastResumeSessionId="00000000-dead-4ead-8ead-000000000000")],
          open(path, "w"), indent=2)
PY
CONV_A1=11111111-aaaa-4aaa-8aaa-111111111111
CONV_A2=22222222-aaaa-4aaa-8aaa-222222222222
CONV_B1=33333333-bbbb-4bbb-8bbb-333333333333
stmux new-session -d -s konsolai-guitest-Claude-a1a1a1a1 -c "$ALPHA" "FAKE_CONVERSATION_ID=$CONV_A1 claude"
stmux new-session -d -s konsolai-guitest-Claude-a2a2a2a2 -c "$ALPHA" "FAKE_CONVERSATION_ID=$CONV_A2 claude"
export RESTART_TEST_SOCK="$SOCK" RESTART_TEST_BETA="$BETA" CONV_A1 CONV_A2 CONV_B1

DISPLAY_NUM=77
while [ -e "/tmp/.X${DISPLAY_NUM}-lock" ]; do
    DISPLAY_NUM=$((DISPLAY_NUM + 1))
done
Xvfb ":${DISPLAY_NUM}" -screen 0 1600x1000x24 >/dev/null 2>&1 &
XVFB_PID=$!

cleanup() {
    kill "$XVFB_PID" 2>/dev/null || true
    stmux kill-server 2>/dev/null || true
    rm -rf "$SANDBOX"
}
trap cleanup EXIT
sleep 0.5
export DISPLAY=":${DISPLAY_NUM}"
export LD_LIBRARY_PATH="$ROOT/build/bin:${LD_LIBRARY_PATH:-}"

dbus-run-session bash -c '
/usr/libexec/at-spi-bus-launcher --launch-immediately >/dev/null 2>&1 &
ATSPI_PID=$!
sleep 0.5
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 QT_QPA_PLATFORM=xcb "'"$BINARY"'" >"'"$SANDBOX"'/konsolai.log" 2>&1 &
KONSOLAI_PID=$!
trap "kill $KONSOLAI_PID $ATSPI_PID 2>/dev/null" EXIT
/usr/bin/python3 "'"$ROOT"'/Testing/gui-restart-live-test.py" "$KONSOLAI_PID"
' 2>&1 | grep -v -e "dbind-WARNING" -e "SpiRegistry"
RESULT=${PIPESTATUS[0]}
if [ "$RESULT" -ne 0 ]; then
    echo "--- konsolai log (last 30 lines) ---"
    grep -a -v -e "qt.accessibility" -e "^$" "$SANDBOX/konsolai.log" 2>/dev/null | tail -40
fi
exit "$RESULT"
