#!/bin/bash
# Run all GUI test suites against the live Konsolai instance.
# Usage: bash Testing/run-all-gui-tests.sh

set -e
cd "$(dirname "$0")/.."

echo "========================================"
echo "Konsolai GUI Test Suite"
echo "========================================"

TOTAL_PASS=0
TOTAL_FAIL=0

run_test() {
    local name="$1"
    local script="$2"
    echo ""
    echo "--- $name ---"
    if /usr/bin/python3 "$script" 2>/dev/null; then
        echo "  ✓ $name: ALL PASSED"
    else
        echo "  ✗ $name: SOME FAILURES"
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
    fi
}

run_test "Smoke Test" "Testing/gui-smoke-test.py"
run_test "Interaction Test" "Testing/gui-interaction-test.py"
run_test "Lifecycle Test" "Testing/gui-lifecycle-test.py"

echo ""
echo "========================================"
if [ $TOTAL_FAIL -eq 0 ]; then
    echo "ALL SUITES PASSED"
    exit 0
else
    echo "$TOTAL_FAIL SUITE(S) HAD FAILURES"
    exit 1
fi
