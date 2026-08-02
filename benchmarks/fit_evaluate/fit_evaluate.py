#!/usr/bin/env python3
"""Fit each algorithm's evaluate(ench_count) time-prediction curve from
benchmark measurements.

Model:  t(e) = a * b^e   (seconds)  — log-linear regression
        ln t = ln a + e * ln b

Deterministic algorithms (all measured times ≈ 0) fit to constant 0.

Usage:
    python fit_evaluate.py [measured.csv] [--json out.json]

Input CSV columns:  algorithm,ench_count,time_ms,status
    status ∈ {completed, timeout}
    timeout rows mark the feasible-range ceiling and are EXCLUDED from the
    regression (but reported so we know where the curve explodes).

Output:
    per-algorithm fitted a / b, a predicted-vs-actual table, and (with
    --json) a machine-readable constants map.
"""
import sys
import csv
import json
import math

# Algorithms known to be deterministic (O(n^k) construction, ~0 ms always).
DETERMINISTIC = {"hamming", "diff_first", "difficulty_first", "penalty_balance"}

DEFAULT_MAX_ENCH = 40

# Safety factor on fitted `a`: predict ~30% slower than the raw regression so
# the tier matrix errs toward skipping (never risks a timeout).
SAFETY = 1.3


def regress(rows):
    """rows: list of (e, t_sec). Fit ln t = c0 + c1*e. Returns (a, b, r2)."""
    n = len(rows)
    if n < 2:
        return None
    sx = sy = sxx = sxy = 0.0
    for e, t in rows:
        if t <= 0:
            continue
        y = math.log(t)
        sx += e; sy += y; sxx += e * e; sxy += e * y
    denom = n * sxx - sx * sx
    if abs(denom) < 1e-12:
        return None
    c1 = (n * sxy - sx * sy) / denom
    c0 = (sy - c1 * sx) / n
    a = math.exp(c0)
    b = math.exp(c1)
    # R²
    ymean = sy / n
    sst = ssr = 0.0
    for e, t in rows:
        if t <= 0:
            continue
        y = math.log(t)
        yhat = c0 + c1 * e
        sst += (y - ymean) ** 2
        ssr += (y - yhat) ** 2
    r2 = 1.0 - ssr / sst if sst > 0 else 0.0
    return (a, b, r2)


def fit_algorithm(alg, rows, max_e):
    """Returns dict describing the fit for one algorithm."""
    completed = [(e, t / 1000.0) for (e, t, status) in rows if status == "completed"]
    timeouts = [e for (e, t, status) in rows if status == "timeout"]

    # Genuinely deterministic: all completed times ≤ 1 ms (and ≥2 samples).
    if alg in DETERMINISTIC or (len(completed) >= 2
                                and all(t <= 0.001 for _, t in completed)):
        return {"algorithm": alg, "model": "constant", "a": 0.0, "b": 0.0,
                "r2": None, "feasible": (min((e for e, _, _ in rows), default=0),
                                          max((e for e, _, _ in rows), default=0)),
                "timeouts": sorted(timeouts), "predict": lambda e: 0.0}

    # Prefer the steep tail (points ≥ 1 s): the noisy low-count points drag b
    # down and under-predict the growth that actually sets the tier boundary.
    # Fall back to all points only when the tail has < 2 samples.
    tail = [p for p in completed if p[1] >= 1.0]
    fit_rows = tail if len(tail) >= 2 else completed

    fit = regress(fit_rows)
    if fit is None:
        # Insufficient samples (e.g. a single completed point).  Report
        # unfitted; the caller supplies a sibling-family curve manually.
        return {"algorithm": alg, "model": "unfitted", "a": None, "b": None,
                "r2": None, "feasible": None, "timeouts": sorted(timeouts),
                "predict": None}

    a, b, r2 = fit
    # Safety margin: predict ~30% slower than the raw fit so the tier matrix
    # skips (rather than risks) an algorithm that would actually time out.
    a *= SAFETY
    return {"algorithm": alg, "model": "exp", "a": a, "b": b, "r2": r2,
            "feasible": (min((e for e, _ in completed), default=0),
                         max((e for e, _ in completed), default=0)),
            "timeouts": sorted(timeouts),
            "predict": (lambda e, a=a, b=b: a * b ** e)}


def main():
    args = sys.argv[1:]
    csv_path = args[0] if args else "measured.csv"
    json_path = None
    if "--json" in args:
        json_path = args[args.index("--json") + 1]

    rows_by_alg = {}
    with open(csv_path, newline="") as f:
        for row in csv.DictReader(f):
            alg = row["algorithm"].strip()
            e = int(row["ench_count"])
            t = int(float(row["time_ms"]))
            status = row["status"].strip()
            rows_by_alg.setdefault(alg, []).append((e, t, status))

    results = []
    print("{:<16} {:<10} {:>10} {:>8} {:>6} {:>12} {}".format(
        "algorithm", "model", "a", "b", "r2", "feasible", "timeouts"))
    print("-" * 72)
    for alg in sorted(rows_by_alg):
        rows = rows_by_alg[alg]
        r = fit_algorithm(alg, rows, DEFAULT_MAX_ENCH)
        results.append(r)
        if r["model"] == "constant":
            print("{:<16} constant      {:>10.4g} {:>8}   {:>12}".format(
                alg, 0, 0, str(r["feasible"])))
        elif r["model"] == "exp":
            print("{:<16} exp          {:>10.4g} {:>8.4f} {:>6.3f} {:>12} {}".format(
                alg, r["a"], r["b"], r["r2"], str(r["feasible"]), r["timeouts"]))
        else:
            print("{:<16} unfitted     (insufficient data)".format(alg))

    # Predicted-vs-actual table for exponential fits
    print("\n=== predicted vs actual (seconds) ===")
    print("{:<16} {:>3} {:>10} {:>10} {:>7}".format(
        "algorithm", "e", "actual_s", "pred_s", "ratio"))
    for r in results:
        if r["model"] != "exp" or r["predict"] is None:
            continue
        for e, t, status in rows_by_alg[r["algorithm"]]:
            pred = r["predict"](e)
            ratio = pred / (t / 1000.0) if t else float("inf")
            print("{:<16} {:>3} {:>10.4f} {:>10.4f} {:>7.2f}".format(
                r["algorithm"], e, t / 1000.0, pred, ratio))

    if json_path:
        out = {}
        for r in results:
            out[r["algorithm"]] = {
                "model": r["model"], "a": r["a"], "b": r["b"], "r2": r["r2"],
                "feasible": r["feasible"], "timeouts": r["timeouts"],
            }
        with open(json_path, "w") as f:
            json.dump(out, f, indent=2)
        print(f"\nwrote {json_path}")


if __name__ == "__main__":
    main()
