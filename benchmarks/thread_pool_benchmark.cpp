// ═══════════════════════════════════════════════════════════════════════════════
// ThreadPool benchmark — throughput, scalability, parallel_for
// ═══════════════════════════════════════════════════════════════════════════════

#include "utils/thread/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace besq;
namespace chrono = std::chrono;
using Clock = chrono::steady_clock;

// ─── Configuration ─────────────────────────────────────────────────────────

#ifdef NDEBUG
constexpr int64_t OPS_BATCH      = 1'000'000;   // tasks per throughput test
constexpr int64_t OPS_PAR_FOR    =  50'000'000;  // indices per parallel_for test
constexpr int64_t OPS_HEAVY      =     100'000;  // heavy tasks (each does real work)
#else
constexpr int64_t OPS_BATCH      =    100'000;
constexpr int64_t OPS_PAR_FOR    =   1'000'000;
constexpr int64_t OPS_HEAVY      =      5'000;
#endif

constexpr int64_t WARMUP_TASKS   =     10'000;

// Timer helper
struct Timer {
    Clock::time_point start = Clock::now();
    double elapsed() const {
        return chrono::duration<double>(Clock::now() - start).count();
    }
    void reset() { start = Clock::now(); }
};

// Performance sink: prevents compiler from eliminating dead stores
struct Sink {
    int64_t val = 0;
    void add(int64_t x) { val += x; }
    // Force a side-effect that the optimiser cannot bypass.
    void escape() { volatile int64_t v = val; (void)v; }
};

// ─── Workload generators ───────────────────────────────────────────────────

// Busy-loop for ~`cycles` iterations — calibrated to ~1 µs on modern HW.
// Using a volatile sink prevents the loop from being eliminated.
inline void busy_work(int64_t cycles, Sink* sink = nullptr) {
    int64_t x = 0;
    for (int64_t i = 0; i < cycles; ++i) { x += i; }
    if (sink) sink->add(x);
    else      { volatile int64_t dummy = x; (void)dummy; }
}

// ─── Benchmark: task submission throughput ──────────────────────────────────

// Measures how many empty (fast) tasks per second the pool can drain.
struct ThroughputResult {
    int      threads;
    int64_t  tasks;
    double   elapsed_ms;
    double   throughput;   // M tasks / s

    void print(const char* label) const {
        std::printf("  %-22s  %4d threads  %7.1f M/s  (%6.0f ms)\n",
                    label, threads, throughput, elapsed_ms);
    }
};

ThroughputResult bench_throughput(ThreadPool& pool, int64_t n_tasks,
                                  const char* label) {
    std::atomic<int64_t> counter{0};

    Timer t;
    for (int64_t i = 0; i < n_tasks; ++i) {
        pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.wait();

    double elapsed = t.elapsed() * 1000.0;  // ms
    double thru = static_cast<double>(n_tasks) / (elapsed / 1000.0) / 1.0e6;

    // Verify correctness
    if (counter.load() != n_tasks)
        std::fprintf(stderr, "  WARN: expected %lld, got %lld\n",
                     (long long)n_tasks, (long long)counter.load());

    return {static_cast<int>(pool.size()), n_tasks, elapsed, thru};
}

// ─── Benchmark: parallel_for throughput ─────────────────────────────────────

struct ParForResult {
    int      threads;
    int64_t  elements;
    int64_t  work_per_element;
    double   elapsed_ms;
    double   throughput;   // M elements / s

    void print(const char* label) const {
        std::printf("  %-22s  %4d threads  %7.1f M/s  (%6.0f ms)  work=%lld\n",
                    label, threads, throughput, elapsed_ms,
                    (long long)work_per_element);
    }
};

template <typename Body>
ParForResult bench_parallel_for(ThreadPool& pool, int64_t n, Body&& body,
                                const char* label, int64_t work = 0) {
    Timer t;
    parallel_for(pool, int64_t{0}, n, std::forward<Body>(body));
    double elapsed = t.elapsed() * 1000.0;
    double thru = static_cast<double>(n) / (elapsed / 1000.0) / 1.0e6;
    return {static_cast<int>(pool.size()), n, work, elapsed, thru};
}

// ─── Main ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Parse optional thread list
    std::vector<int> thread_counts;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            thread_counts.push_back(std::atoi(argv[i]));
        }
    } else {
        thread_counts = {1, 2, 4, 8,
                         static_cast<int>(std::thread::hardware_concurrency())};
        // Remove duplicates and sort
        std::sort(thread_counts.begin(), thread_counts.end());
        thread_counts.erase(
            std::unique(thread_counts.begin(), thread_counts.end()),
            thread_counts.end());
    }

    std::printf("╔══════════════════════════════════════════════════════════════╗\n");
    std::printf("║            ThreadPool Benchmark                             ║\n");
    std::printf("╚══════════════════════════════════════════════════════════════╝\n");
    std::printf("  Hardware concurrency: %zu\n", static_cast<std::size_t>(std::thread::hardware_concurrency()));
    std::printf("  Task batch size:      %lld\n", (long long)OPS_BATCH);
    std::printf("  parallel_for size:    %lld\n", (long long)OPS_PAR_FOR);
    std::printf("\n");

    // ── Warm-up (cold start, first allocation, thread setup) ─────────
    {
        ThreadPool warmup(thread_counts.back());
        for (int64_t i = 0; i < WARMUP_TASKS; ++i)
            warmup.submit([] { busy_work(10); });
        warmup.wait();
    }
    std::printf("  Warm-up complete (%lld tasks)\n\n", (long long)WARMUP_TASKS);

    // ═══════════════════════════════════════════════════════════════════
    //  1. Task throughput — fine-grained (empty tasks)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("── Fine-grained task throughput ───────────────────────────\n");
    for (int tc : thread_counts) {
        ThreadPool pool(tc);
        auto r = bench_throughput(pool, OPS_BATCH, "empty task");
        r.print("empty task");
    }

    // ═══════════════════════════════════════════════════════════════════
    //  2. Task throughput — medium work (~1 µs per task)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("\n── Medium task throughput (≈1 µs work) ───────────────────\n");
    for (int tc : thread_counts) {
        ThreadPool pool(tc);
        auto r = bench_throughput(pool, OPS_BATCH, "1 µs task");
        r.print("1 µs task");
    }

    // ═══════════════════════════════════════════════════════════════════
    //  3. Heavy task throughput (~100 µs work per task)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("\n── Heavy task throughput (≈100 µs work) ──────────────────\n");
    for (int tc : thread_counts) {
        ThreadPool pool(tc);
        auto r = bench_throughput(pool, OPS_HEAVY, "100 µs task");
        r.print("100 µs task");
    }

    // ═══════════════════════════════════════════════════════════════════
    //  4. parallel_for — tiny work (just store)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("\n── parallel_for throughput (store only) ──────────────────\n");
    for (int tc : thread_counts) {
        ThreadPool pool(tc);
        std::vector<int64_t> data(OPS_PAR_FOR);
        auto r = bench_parallel_for(pool, OPS_PAR_FOR,
            [&](int64_t i) { data[i] = i; }, "store only");
        r.print("store only");
    }

    // ═══════════════════════════════════════════════════════════════════
    //  5. parallel_for — medium work (sqrt + write)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("\n── parallel_for throughput (sqrt + write) ────────────────\n");
    for (int tc : thread_counts) {
        ThreadPool pool(tc);
        std::vector<double> data(OPS_PAR_FOR);
        auto r = bench_parallel_for(pool, OPS_PAR_FOR,
            [&](int64_t i) { data[i] = std::sqrt(static_cast<double>(i)); },
            "sqrt + write");
        r.print("sqrt + write");
    }

    // ═══════════════════════════════════════════════════════════════════
    //  6. Scalability: speedup relative to 1 thread (empty tasks)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("\n── Scalability (empty tasks, speedup vs 1 thread) ────\n");
    {
        ThreadPool pool1(1);
        auto base = bench_throughput(pool1, OPS_BATCH, "baseline (1T)");
        double base_thru = base.throughput;

        for (int tc : thread_counts) {
            if (tc == 1) continue;
            ThreadPool pool(tc);
            auto r = bench_throughput(pool, OPS_BATCH, "scalability");
            double speedup = r.throughput / base_thru;
            std::printf("  scalability           %4d threads  %6.2fx speedup  (%7.1f M/s)\n",
                        tc, speedup, r.throughput);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  7. parallel_for scalability (sqrt, speedup vs 1 thread)
    // ═══════════════════════════════════════════════════════════════════
    std::printf("\n── parallel_for scalability (sqrt, speedup vs 1 thread) ─\n");
    {
        ThreadPool pool1(1);
        std::vector<double> base_data(OPS_PAR_FOR);
        auto base = bench_parallel_for(pool1, OPS_PAR_FOR,
            [&](int64_t i) { base_data[i] = std::sqrt(static_cast<double>(i)); },
            "baseline (1T)");
        double base_thru = base.throughput;

        for (int tc : thread_counts) {
            if (tc == 1) continue;
            ThreadPool pool(tc);
            std::vector<double> data(OPS_PAR_FOR);
            auto r = bench_parallel_for(pool, OPS_PAR_FOR,
                [&](int64_t i) { data[i] = std::sqrt(static_cast<double>(i)); },
                "scalability");
            double speedup = r.throughput / base_thru;
            std::printf("  scalability           %4d threads  %6.2fx speedup  (%7.1f M/s)\n",
                        tc, speedup, r.throughput);
        }
    }

    std::printf("\n══════════════════════════════════════════════════════════════\n");
    std::printf("  Done.\n");
    std::printf("══════════════════════════════════════════════════════════════\n");

    return 0;
}
