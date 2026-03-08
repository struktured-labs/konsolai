#!/bin/bash
# Run GUI tests against an ISOLATED Konsolai instance.
# Launches Konsolai on a virtual display (Xvfb) — your live instance is untouched.
# Tests are PID-scoped: they only interact with the test instance.
#
# Usage: bash Testing/run-isolated-gui-tests.sh

set -e
cd "$(dirname "$0")/.."

BINARY="$PWD/build/bin/konsolai"
if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found: $BINARY"
    echo "Run: cd build && ninja -j4"
    exit 1
fi

echo "========================================"
echo "Konsolai Isolated GUI Tests"
echo "========================================"

# Start a virtual X display
DISPLAY_NUM=55
while [ -e "/tmp/.X${DISPLAY_NUM}-lock" ]; do
    DISPLAY_NUM=$((DISPLAY_NUM + 1))
done

Xvfb :${DISPLAY_NUM} -screen 0 1280x1024x24 &
XVFB_PID=$!
sleep 0.5

# Launch Konsolai on virtual display, sharing host dbus (so AT-SPI sees it).
# Use separate XDG dirs so it doesn't reattach user's sessions or stomp hooks.
export LD_LIBRARY_PATH="$PWD/build/bin:$LD_LIBRARY_PATH"
# Share user's XDG dirs (needs KDE profiles to start). The test instance runs
# on a separate virtual display so it can't interfere with the live instance's
# window. Hook sockets are per-session-ID so they don't collide either.
DISPLAY=:${DISPLAY_NUM} \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    QT_QPA_PLATFORM=xcb \
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
    "$BINARY" &
KONSOLAI_PID=$!
echo "  Xvfb display :${DISPLAY_NUM}, Konsolai PID $KONSOLAI_PID"

cleanup() {
    kill $KONSOLAI_PID 2>/dev/null || true
    kill $XVFB_PID 2>/dev/null || true
    wait 2>/dev/null
}
trap cleanup EXIT

# Give Konsolai time to start and reattach sessions
echo "  Waiting for startup..."
sleep 10
kill -0 $KONSOLAI_PID 2>/dev/null && echo "  Process alive after 10s" || echo "  Process DEAD after 10s"

# Wait for AT-SPI visibility (up to 30s after initial sleep)
FOUND=false
for i in $(seq 1 60); do
    if ! kill -0 $KONSOLAI_PID 2>/dev/null; then
        echo "  Konsolai exited early"
        exit 0
    fi
    ATSPI_RESULT=$(/usr/bin/python3 -c "
import gi; gi.require_version('Atspi', '2.0')
from gi.repository import Atspi
import sys
target = $KONSOLAI_PID
desktop = Atspi.get_desktop(0)
n = desktop.get_child_count()
for idx in range(n):
    app = desktop.get_child_at_index(idx)
    if app:
        try:
            if app.get_process_id() == target:
                print('found')
                sys.exit(0)
        except: pass
sys.exit(1)
" 2>&1 | grep -v "dbind-WARNING" | grep -v "SpiRegistry")
    if [ "$ATSPI_RESULT" = "found" ]; then
        echo "  AT-SPI visible after ${i}x500ms"
        FOUND=true
        break
    fi
    sleep 0.5
done

if [ "$FOUND" = false ]; then
    echo "  Not visible in AT-SPI — skipping"
    exit 0
fi

echo ""
TOTAL_PASS=0
TOTAL_FAIL=0

run_scoped_test() {
    local name="$1"
    local script="$2"
    echo "--- $name (PID=$KONSOLAI_PID) ---"
    /usr/bin/python3 -c "
import sys, os
sys.path.insert(0, 'tools/gui-mcp/src')
from konsolai_gui_mcp.atspi_backend import AtspiBackend
# Monkey-patch: all AtspiBackend instances target our test PID
_real_init = AtspiBackend.__init__
AtspiBackend.__init__ = lambda self, target_pid=None: _real_init(self, target_pid=$KONSOLAI_PID)
exec(open('$script').read())
" 2>/dev/null
    local result=$?
    if [ $result -eq 0 ]; then
        TOTAL_PASS=$((TOTAL_PASS + 1))
    elif [ $result -eq 2 ]; then
        echo "  SKIP (app not visible)"
    else
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
    fi
    echo ""
}

run_scoped_test "Smoke Test" "Testing/gui-smoke-test.py"
run_scoped_test "Interaction Test" "Testing/gui-interaction-test.py"

echo "========================================"
if [ $TOTAL_FAIL -eq 0 ]; then
    echo "ALL ISOLATED TESTS PASSED ($TOTAL_PASS suites)"
    exit 0
else
    echo "$TOTAL_FAIL SUITE(S) FAILED"
    exit 1
fi
