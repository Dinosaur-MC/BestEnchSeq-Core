#!/usr/bin/env bash
# Final integration verification: build all, run all 89 test binaries,
# compare the 88 migrated targets against the pre-migration baseline.
set -u
cd "$(dirname "$0")/.."

echo "=== 1. Full build ==="
flock build/lock -c "cmake --build build" || { echo "BUILD FAILED"; exit 1; }

echo "=== 2. Run all test binaries ==="
mismatch=0
for t in $(cd build/bin && ls test_*.exe | sed 's/\.exe$//' | sort); do
    ./build/bin/$t.exe > /tmp/f_$t.log 2>&1
    rc=$?
    res=$(grep -o "Results: [0-9]* passed, [0-9]* failed, [0-9]* total" /tmp/f_$t.log | tail -1)
    if [ "$t" = "test_framework_test" ]; then
        echo "$t rc=$rc $res (bare run: negative cases expected)"
        continue
    fi
    base=$(grep "^$t " build/test-results-baseline.txt)
    # baseline entry format: "<name> exit=<rc> Results: ..." — compare the Results part
    bres=$(echo "$base" | grep -o "Results: [0-9]* passed, [0-9]* failed, [0-9]* total" | tail -1)
    case "$t" in
        test_sandbox)
            # 已知例外：旧二进制插件缺失时 SKIP 后直接 return（无 Results 行）；
            # 新框架输出 Results + Skipped。接受 0 failed + rc=0。
            if [ "$rc" = "0" ] && echo "$res" | grep -q "0 failed"; then
                echo "$t rc=$rc OK(exception) $res"
            else
                echo "$t rc=$rc MISMATCH! $res | BASE: $base"; mismatch=1
            fi
            ;;
        test_web_integration)
            # 已知例外：既有时序 flake，轮询循环内条件执行断言数 ±1（92/93，
            # 0 failed，实测三次 92/92/93）。接受 0 failed + rc=0。
            if [ "$rc" = "0" ] && echo "$res" | grep -q "0 failed"; then
                echo "$t rc=$rc OK(flake ±1) $res | BASE: $base"
            else
                echo "$t rc=$rc MISMATCH! $res | BASE: $base"; mismatch=1
            fi
            ;;
        *)
            if [ "$res" = "$bres" ]; then
                echo "$t rc=$rc OK  $res"
            else
                echo "$t rc=$rc MISMATCH! $res | BASE: $base"
                mismatch=1
            fi
            ;;
    esac
done
echo "=== comparison done (mismatch=$mismatch) ==="
exit $mismatch
