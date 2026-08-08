// ═══════════════════════════════════════════════════════════════════════════
// ThreadPool benchmark — throughput, scalability, parallel_for
//
// Migrated to the shared benchmark harness (benchmarks/framework/
// bench_framework.h): one case per (workload × thread-count) point; the
// thread pool (and the parallel_for data vector) is rebuilt per iteration
// by the case's setup/teardown hooks (untimed), and the harness owns
// warmup / iterations / statistics / CLI
// (--list / --filter / --iterations / --warmup / --json).
//
// Run: build/bin/thread_pool_benchmark [options]
// ═══════════════════════════════════════════════════════════════════════════

#define BESQ_BENCH_MAIN
#include "framework/bench_framework.h"

#include "utils/thread/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using namespace besq;

// ─── Configuration ─────────────────────────────────────────────────────────

#ifdef NDEBUG
constexpr int64_t OPS_BATCH   =     500'000;   // tasks per throughput test
constexpr int64_t OPS_PAR_FOR =  10'000'000;  // indices per parallel_for test
constexpr int64_t OPS_HEAVY   =     100'000;  // heavy tasks (each does real work)
#else
constexpr int64_t OPS_BATCH   =     100'000;
constexpr int64_t OPS_PAR_FOR =   1'000'000;
constexpr int64_t OPS_HEAVY   =       5'000;
#endif

// ─── Performance sink & workload generators ────────────────────────────────

// Prevents compiler from eliminating dead stores
struct Sink {
    int64_t val = 0;
    void add(int64_t x) { val += x; }
    // Force a side-effect that the optimiser cannot bypass.
    void escape() { volatile int64_t v = val; (void)v; }
};

// Busy-loop for ~`cycles` iterations — calibrated to ~1 µs on modern HW.
// Using a volatile sink prevents the loop from being eliminated.
inline void busy_work(int64_t cycles, Sink* sink = nullptr) {
    double x = 0;
    for (int64_t i = 0; i < cycles; ++i) { x += std::log(i + 2); }
    if (sink) sink->add(x);
    else { volatile int64_t dummy = x; (void)dummy; }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Measurement units (one unit per harness iteration; no internal timing —
//  the harness times the whole call)
// ═══════════════════════════════════════════════════════════════════════════

// Task submission throughput: submit n_tasks and wait for the pool to drain.
// Verifies the completion counter as a sanity check.
static void bench_throughput(ThreadPool& pool, int64_t n_tasks) {
    std::atomic<int64_t> counter{0};

    for (int64_t i = 0; i < n_tasks; ++i) {
        pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.wait();

    if (counter.load() != n_tasks)
        std::fprintf(stderr, "  WARN: expected %lld, got %lld\n",
                     (long long)n_tasks, (long long)counter.load());
}

// parallel_for throughput: run body over [0, n).
template <typename Body>
static void bench_parallel_for(ThreadPool& pool, int64_t n, Body&& body) {
    parallel_for(pool, int64_t{0}, n, std::forward<Body>(body));
}

// ─── Per-iteration fixture ─────────────────────────────────────────────────
// The thread pool (and, for parallel_for cases, the data vector) is rebuilt
// by the case setup hook so pool construction never lands inside the timed
// region.  These are file-scope statics referenced by the case lambdas.
static std::unique_ptr<ThreadPool> g_pool;
static std::vector<int64_t> g_data_i64;
static std::vector<double> g_data_f64;

static void fixture_pool_setup(int tc) {
    g_pool = std::make_unique<ThreadPool>(static_cast<std::size_t>(tc));
}
static void fixture_pool_teardown() {
    g_pool.reset();
}
static void fixture_i64_setup(int tc) {
    fixture_pool_setup(tc);
    g_data_i64.resize(static_cast<std::size_t>(OPS_PAR_FOR)); // body rewrites every index
}
static void fixture_f64_setup(int tc) {
    fixture_pool_setup(tc);
    g_data_f64.resize(static_cast<std::size_t>(OPS_PAR_FOR));
}
static void fixture_i64_teardown() {
    g_data_i64.clear();
    g_data_f64.clear();
    fixture_pool_teardown();
}
static void fixture_f64_teardown() {
    g_data_f64.clear();
    fixture_pool_teardown();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Case registration — data-driven: (workload × thread-count) matrix.
//  Thread counts: 1, 2, 4, 8, hardware_concurrency (dedup, sorted).
// ═══════════════════════════════════════════════════════════════════════════

namespace {
std::vector<int> make_thread_counts() {
    std::vector<int> tc = {1, 2, 4, 8,
                           static_cast<int>(std::thread::hardware_concurrency())};
    std::sort(tc.begin(), tc.end());
    tc.erase(std::unique(tc.begin(), tc.end()), tc.end());
    return tc;
}
std::string thread_label(int tc) {
    return std::to_string(tc) + (tc == 1 ? " thread" : " threads");
}
} // namespace

[[maybe_unused]] static const bool s_registered = [] {
    auto& reg = ::bench::registry();
    const std::vector<int> counts = make_thread_counts();

    auto add_case = [&reg](std::string name, std::string group, int64_t ops,
                           std::string baseline, std::function<void()> setup,
                           std::function<void()> teardown,
                           std::function<void()> body) {
        reg.push_back(bench::Case{std::move(name), std::move(body),
                                  std::move(setup), std::move(teardown), 0,
                                  std::move(group), ops, std::move(baseline)});
    };

    // ── 1. Fine-grained task throughput (empty tasks; tc <= 4) ──────────
    for (int tc : counts) {
        if (tc > 4) continue;
        add_case("empty task throughput (" + thread_label(tc) + ")",
                 "Fine-grained task throughput", OPS_BATCH,
                 tc == 1 ? "" : "empty task throughput (1 thread)",
                 [tc] { fixture_pool_setup(tc); }, fixture_pool_teardown,
                 [] { bench_throughput(*g_pool, OPS_BATCH); });
    }

    // ── 2. Heavy task throughput (~100 µs work per task; tc <= 8) ───────
    for (int tc : counts) {
        if (tc > 8) continue;
        add_case("heavy task throughput (" + thread_label(tc) + ")",
                 "Heavy task throughput", OPS_HEAVY,
                 tc == 1 ? "" : "heavy task throughput (1 thread)",
                 [tc] { fixture_pool_setup(tc); }, fixture_pool_teardown,
                 [] { bench_throughput(*g_pool, OPS_HEAVY); });
    }

    // ── 3. parallel_for — tiny work (just store) ────────────────────────
    for (int tc : counts) {
        add_case("parallel_for store only (" + thread_label(tc) + ")",
                 "parallel_for throughput (store only)", OPS_PAR_FOR,
                 tc == 1 ? "" : "parallel_for store only (1 thread)",
                 [tc] { fixture_i64_setup(tc); }, fixture_i64_teardown,
                 [] { bench_parallel_for(*g_pool, OPS_PAR_FOR,
                                          [](int64_t i) { g_data_i64[i] = i; }); });
    }

    // ── 4. parallel_for — medium work (sqrt + write) ────────────────────
    for (int tc : counts) {
        add_case("parallel_for sqrt (" + thread_label(tc) + ")",
                 "parallel_for throughput (sqrt + write)", OPS_PAR_FOR,
                 tc == 1 ? "" : "parallel_for sqrt (1 thread)",
                 [tc] { fixture_f64_setup(tc); }, fixture_f64_teardown,
                 [] { bench_parallel_for(*g_pool, OPS_PAR_FOR,
                                          [](int64_t i) {
                                              g_data_f64[i] = std::sqrt(static_cast<double>(i));
                                          }); });
    }

    // ── 5. parallel_for scalability (sqrt + log work) ───────────────────
    for (int tc : counts) {
        add_case("parallel_for scalability (" + thread_label(tc) + ")",
                 "parallel_for scalability", OPS_PAR_FOR,
                 tc == 1 ? "" : "parallel_for scalability (1 thread)",
                 [tc] { fixture_f64_setup(tc); }, fixture_f64_teardown,
                 [] { bench_parallel_for(*g_pool, OPS_PAR_FOR,
                                          [](int64_t i) {
                                              g_data_f64[i] = std::sqrt(static_cast<double>(i));
                                              busy_work(250);
                                          }); });
    }

    return true;
}();
