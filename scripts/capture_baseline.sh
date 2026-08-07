#!/usr/bin/env bash
# Capture pre-migration baseline: exit code + Results line per test binary.
# Writes build/test-results-baseline.txt (repo root cwd required).
set -u
cd "$(dirname "$0")/.."
: > build/test-results-baseline.txt
for t in $(cd build/bin && ls test_*.exe | sed 's/\.exe$//' | sort); do
    ./build/bin/$t.exe > /tmp/base_$t.log 2>&1
    rc=$?
    res=$(grep -o "Results: [0-9]* passed, [0-9]* failed, [0-9]* total" /tmp/base_$t.log | tail -1)
    echo "$t exit=$rc $res" >> build/test-results-baseline.txt
done
wc -l build/test-results-baseline.txt
