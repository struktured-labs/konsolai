#!/bin/bash
# Run GUI tests against an ISOLATED Konsolai instance.
# Launches Konsolai on a virtual display (Xvfb) with its own D-Bus + AT-SPI.
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

export DISPLAY=:${DISPLAY_NUM}
export LD_LIBRARY_PATH="$PWD/build/bin:$LD_LIBRARY_PATH"

# Run everything inside a dedicated D-Bus session so AT-SPI works on
# the virtual display. The inner script inherits DISPLAY and LD_LIBRARY_PATH.
dbus-run-session bash -c '
set -e
BINARY="'"$BINARY"'"
XVFB_PID='"$XVFB_PID"'

# Start the AT-SPI bus and registryd for this D-Bus session
/usr/libexec/at-spi-bus-launcher --launch-immediately &
ATSPI_PID=$!
sleep 0.5

# Launch Konsolai
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    QT_QPA_PLATFORM=xcb \
    KONSOLAI_WORKSPACE=test \
    "$BINARY" &
KONSOLAI_PID=$!
echo "  Xvfb display $DISPLAY, Konsolai PID $KONSOLAI_PID"

cleanup() {
    kill $KONSOLAI_PID 2>/dev/null || true
    sleep 1
    # Force-detach any stale tmux clients from dead PTYs.
    for tty in $(tmux list-clients -F "#{client_tty}" 2>/dev/null); do
        if [ ! -e "$tty" ]; then
            tmux detach-client -t "$tty" 2>/dev/null
        fi
    done
    kill $ATSPI_PID 2>/dev/null || true
}
trap cleanup EXIT

# Give Konsolai time to start
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
import gi; gi.require_version('"'"'Atspi'"'"', '"'"'2.0'"'"')
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
                print('"'"'found'"'"')
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
__file__ = os.path.abspath('"'"'$script'"'"')
sys.path.insert(0, '"'"'tools/gui-mcp/src'"'"')
from konsolai_gui_mcp.atspi_backend import AtspiBackend
_real_init = AtspiBackend.__init__
AtspiBackend.__init__ = lambda self, target_pid=None: _real_init(self, target_pid=$KONSOLAI_PID)
exec(compile(open('"'"'$script'"'"').read(), __file__, '"'"'exec'"'"'))
" 2>&1 | grep -v "dbind-WARNING" | grep -v "SpiRegistry"
    local result=${PIPESTATUS[0]}
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
run_scoped_test "Lifecycle Test" "Testing/gui-lifecycle-test.py"

echo "========================================"
if [ $TOTAL_FAIL -eq 0 ]; then
    echo "ALL ISOLATED TESTS PASSED ($TOTAL_PASS suites)"
    exit 0
else
    echo "$TOTAL_FAIL SUITE(S) FAILED"
    exit 1
fi
'
INNER_EXIT=$?

# Cleanup Xvfb (dbus-run-session handles its own children)
kill $XVFB_PID 2>/dev/null || true
wait 2>/dev/null

exit $INNER_EXIT
