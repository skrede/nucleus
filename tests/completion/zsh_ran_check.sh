#!/bin/sh
# Runs every zsh smoke test in the named build tree and fails if any of them skipped. A skip, for want of a
# zsh with zsh/zpty, would otherwise read as a pass.
if [ "$#" -ne 1 ]; then
    echo "usage: sh zsh_ran_check.sh <build directory>" >&2
    exit 2
fi
log=$(mktemp) || exit 1
trap 'rm -f "$log"' EXIT
ctest --test-dir "$1" -R '^zsh_.*_smoke_test$' -j1 --no-tests=error --output-on-failure --timeout 300 > "$log" 2>&1
status=$?
cat "$log"
if [ "$status" -ne 0 ]; then
    exit "$status"
fi
if grep -q 'Skipped' "$log"; then
    echo "a zsh smoke test skipped: no zsh with zsh/zpty was reachable" >&2
    exit 1
fi
