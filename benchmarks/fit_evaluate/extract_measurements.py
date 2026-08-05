#!/usr/bin/env python3
"""Extract per-algorithm (ench_count → time_ms) measurements from a
forge_benchmark log into the fit_evaluate measured.csv.

Policy (driven by evaluate()'s purpose — conservative skip decisions):
  - Multiple datasets share an enchant count (different item / max_cost), and
    measured times differ wildly (e.g. astar 9 enchs: 406 ms … 3712 ms).  Take
    the MAX observed time per (algorithm, count): the curve tracks the worst
    case, so a case that actually takes T seconds is never predicted faster.
  - Rows the benchmark skipped (predicted over budget / "too many enchants")
    carry no measurement; they are recorded as `timeout` with the run's tier
    budget as the time — they mark the feasible-range ceiling and are excluded
    from the regression.
  - `no solution` rows carry no timing at all → dropped (no row).

Usage:
    python extract_measurements.py [benchmark.txt] [--out measured.csv]
"""
import re
import sys

DATASET_RE = re.compile(r"^(\S+) \((\d+) enchants, max \d+L\):$")
DONE_RE    = re.compile(r"^  (\S+)\s+\d+L\s+(?:✅|⚠)\s+(\d+)ms$")
SKIP_RE    = re.compile(r"^  (\S+)\s+SKIP")

BUDGET_MS = 10000  # tier-4 budget (10 s) used by the best_benchmark run


def extract(path):
    time_max = {}   # (algorithm, count) -> max completed time_ms
    skipped = set()  # (algorithm, count) seen SKIP'd
    count = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = DATASET_RE.match(line)
            if m:
                count = int(m.group(2))
                continue
            m = DONE_RE.match(line)
            if m:
                key = (m.group(1), count)
                time_max[key] = max(time_max.get(key, 0), int(m.group(2)))
                continue
            m = SKIP_RE.match(line)
            if m:
                skipped.add((m.group(1), count))

    rows = []
    for alg in sorted({a for a, _ in time_max} | {a for a, _ in skipped}):
        counts = sorted({c for a, c in time_max if a == alg}
                        | {c for a, c in skipped if a == alg})
        for c in counts:
            t = time_max.get((alg, c))
            rows.append((alg, c, t if t is not None else BUDGET_MS,
                         "completed" if t is not None else "timeout"))
    return rows


def main():
    args = sys.argv[1:]
    src = args[0] if args else "best_benchmark.txt"
    out = "measured.csv"
    if "--out" in args:
        out = args[args.index("--out") + 1]

    rows = extract(src)
    with open(out, "w", newline="") as f:
        f.write("algorithm,ench_count,time_ms,status\n")
        for alg, c, t, status in rows:
            f.write(f"{alg},{c},{t},{status}\n")
    print(f"wrote {len(rows)} rows -> {out}")


if __name__ == "__main__":
    main()
