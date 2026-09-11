#!/usr/bin/env bash
# Runs the keycard-simulator tests and fails unless every one of them actually passed.
# The tests skip when no simulator is listening, and a skip exits 0, so ctest's own exit
# code cannot tell "tested" from "tested nothing".
#
# Usage: run-simulator-tests.sh <build-dir>
set -uo pipefail

BUILD_DIR="${1:?usage: $0 <build-dir>}"
EXPECTED=(
    test_simulator_smoke
    test_simulator_pin_blocking
    test_simulator_puk_unblock
    test_simulator_factory_reset
)

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

(cd "$BUILD_DIR" && ctest -R '^test_simulator_' --no-tests=error --output-on-failure --verbose) 2>&1 | tee "$LOG"
ctest_rc=${PIPESTATUS[0]}

failed=0
fail() { echo "::error::$*"; failed=1; }

[ "$ctest_rc" -eq 0 ] || fail "ctest exited $ctest_rc"

while IFS= read -r line; do
    name="$(sed -E 's/.*Test +#[0-9]+: ([^ ]+).*/\1/' <<<"$line")"
    fail "$name was skipped: $line"
done < <(grep -E 'Test +#[0-9]+: .*Skipped' "$LOG")

while IFS= read -r line; do
    fail "a test function was skipped: $line"
done < <(grep -E '^([0-9]+: )?SKIP +: ' "$LOG")

passed=0
for t in "${EXPECTED[@]}"; do
    if grep -qE "Test +#[0-9]+: ${t} \.+ +Passed" "$LOG"; then
        passed=$((passed + 1))
    else
        fail "$t did not pass"
    fi
done

echo "simulator tests passed: $passed/${#EXPECTED[@]}"
[ "$passed" -eq "${#EXPECTED[@]}" ] || failed=1
exit "$failed"
