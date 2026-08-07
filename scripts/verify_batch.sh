#!/usr/bin/env bash
# Verify a batch of test targets against the baseline (Results line must match).
# Usage: bash scripts/verify_batch.sh <target> [<target> ...]
set -u
cd "$(dirname "$0")/.."
fail=0
for t in "$@"; do
    ./build/bin/$t.exe > /tmp/v_$t.log 2>&1
    rc=$?
    res=$(grep -o "Results: [0-9]* passed, [0-9]* failed, [0-9]* total" /tmp/v_$t.log | tail -1)
    base=$(grep "^$t " build/test-results-baseline.txt)
    echo "$t rc=$rc | $res | BASE: $base"
    if [ -z "$res" ]; then echo "  !! no Results line"; fail=1; fi
done
exit $fail
