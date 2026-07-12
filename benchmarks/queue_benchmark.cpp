#include "utils/queue/SPSCQueue.hpp"
#include "utils/queue/BoundedMPMCQueue.hpp"
#include "utils/queue/SegmentedMPMCQueue.hpp"
#include "utils/queue/IQueue.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
//  Configuration
// ═══════════════════════════════════════════════════════════════════════════

// Operation counts — Release needs more work for stable timing.
// SegmentedMPMCQueue has its own lower count: being unbounded it never
// frees blocks until destruction (~12 KB per 1024 items × OPS).
#ifdef NDEBUG
constexpr int64_t OPS_SEQ    = 200'000'000;  // push-pop pairs (bounded queues)
constexpr int64_t OPS_SEG    =  20'000'000;  // SegmentedMPMCQueue (unbounded)
constexpr int64_t OPS_VIRT   =  50'000'000;  // pairs for virtual-dispatch test
#else
constexpr int64_t OPS_SEQ    =  30'000'000;
constexpr int64_t OPS_SEG    =   5'000'000;
constexpr int64_t OPS_VIRT   =  10'000'000;
#endif
constexpr int64_t WARM_SEQ   =   1'000'000;  // warm-up pairs
constexpr int     N_LATENCY  =      10'000;  // latency samples

// ─── Performance-sink helper ────────────────────────────────────────────
// Accumulates checksum from popped values so the compiler cannot eliminate
// push/pop as dead code.  A volatile write at the end enforces the side
// effect across all optimisation levels.

struct Sink {
    int64_t val = 0;
    void add(int64_t v) noexcept { val ^= v; }
    void flush() noexcept { volatile int64_t _ = val; (void)_; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Timer
// ═══════════════════════════════════════════════════════════════════════════

using Clock = std::chrono::high_resolution_clock;

struct Timer {
    Clock::time_point start = Clock::now();
    double elapsed_s() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   Clock::now() - start).count() / 1'000'000.0;
    }
};

static void print_result(const char* label, double elapsed_s, int64_t items) {
    double rate = static_cast<double>(items) / elapsed_s / 1'000'000.0;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "  %-42s %8.2f M/s  (%5.2f s)",
                  label, rate, elapsed_s);
    std::cout << buf << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Sequential throughput  (push-pop pairs)
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_seq(Queue& q, int64_t n_pairs, int64_t warmup,
                      const char* label)
{
    Sink sink;
    int v;

    for (int64_t i = 0; i < warmup; ++i) {
        while (!q.try_push(static_cast<int>(i))) {}
        q.try_pop(v);
    }

    auto t0 = Clock::now();
    for (int64_t i = 0; i < n_pairs; ++i) {
        int val = static_cast<int>(i & 0x7FFFFFFF);
        while (!q.try_push(val)) {}
        q.try_pop(v);
        sink.add(v);
    }
    sink.flush();

    double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t0).count() / 1'000'000.0;
    print_result(label, sec, n_pairs);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Round-trip latency
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_latency(Queue& q, int64_t n, const char* label) {
    int v;
    for (int i = 0; i < 2000; ++i) {
        while (!q.try_push(i)) {}
        q.try_pop(v);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        auto t0 = Clock::now();
        while (!q.try_push(42)) {}
        q.try_pop(v);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              t1 - t0).count());
    }

    std::sort(samples.begin(), samples.end());
    double median = samples[static_cast<size_t>(n) / 2];
    double mean   = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(n);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "  %-42s %6.1f ns median  (mean %6.1f ns)",
                  label, median, mean);
    std::cout << buf << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Virtual-dispatch overhead  (IQueue vs direct)
// ═══════════════════════════════════════════════════════════════════════════

static void bench_virtual(int64_t n_pairs, int64_t warmup) {
    int v;

    // 1. Direct SPSCQueue (baseline)
    {
        Sink sink;
        SPSCQueue<int, 4096> q;
        for (int64_t i = 0; i < warmup; ++i) {
            while (!q.try_push(static_cast<int>(i))) {}
            q.try_pop(v);
        }
        auto t0 = Clock::now();
        for (int64_t i = 0; i < n_pairs; ++i) {
            while (!q.try_push(i)) {}
            q.try_pop(v);
            sink.add(v);
        }
        sink.flush();
        double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                         Clock::now() - t0).count() / 1'000'000.0;
        print_result("SPSCQueue direct (baseline)", sec, n_pairs);
    }

    // 2. QueueAdaptor::underlying() — no virtual dispatch
    {
        Sink sink;
        QueueAdaptor<int, SPSCQueue<int, 4096>> adapted;
        for (int64_t i = 0; i < warmup; ++i) {
            while (!adapted.underlying().try_push(i)) {}
            adapted.underlying().try_pop(v);
        }
        auto t0 = Clock::now();
        for (int64_t i = 0; i < n_pairs; ++i) {
            while (!adapted.underlying().try_push(i)) {}
            adapted.underlying().try_pop(v);
            sink.add(v);
        }
        sink.flush();
        double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                         Clock::now() - t0).count() / 1'000'000.0;
        print_result("QueueAdaptor::underlying()", sec, n_pairs);
    }

    // 3. Virtual dispatch through IQueue<T>
    {
        Sink sink;
        QueueAdaptor<int, SPSCQueue<int, 4096>> adapted;
        IQueue<int>& virt = adapted;
        for (int64_t i = 0; i < warmup; ++i) {
            while (!virt.try_push(static_cast<int>(i))) {}
            virt.try_pop(v);
        }
        auto t0 = Clock::now();
        for (int64_t i = 0; i < n_pairs; ++i) {
            while (!virt.try_push(static_cast<int>(i))) {}
            virt.try_pop(v);
            sink.add(v);
        }
        sink.flush();
        double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                         Clock::now() - t0).count() / 1'000'000.0;
        print_result("IQueue virtual dispatch", sec, n_pairs);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    Timer total;

    std::cout << "╔══════════════════════════════════════════════════════════╗\n"
              << "║            Lock-Free Queue Benchmarks                    ║\n"
              << "╚══════════════════════════════════════════════════════════╝\n"
              << "  Pairs per test: " << OPS_SEQ / 1'000'000 << "M"
              << "  (segmented: " << OPS_SEG / 1'000'000 << "M)"
              << "  warm-up: " << WARM_SEQ / 1'000'000 << "M\n"
              << "  Hardware concurrency: "
              << std::thread::hardware_concurrency() << "\n\n";

    // ── Sequential throughput ─────────────────────────────────────────
    {
        Timer sec;
        std::cout << "── Sequential throughput ────────────────────────\n";
        { SPSCQueue<int, 4096> q; bench_seq(q, OPS_SEQ, WARM_SEQ, "SPSCQueue"); }
        { BoundedMPMCQueue<int, 4096> q; bench_seq(q, OPS_SEQ, WARM_SEQ, "BoundedMPMCQueue"); }
        { SegmentedMPMCQueue<int, 1024> q; bench_seq(q, OPS_SEG, WARM_SEQ, "SegmentedMPMCQueue"); }
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    // ── Round-trip latency ───────────────────────────────────────────
    {
        Timer sec;
        std::cout << "── Round-trip latency ───────────────────────────\n";
        { SPSCQueue<int, 64> q; bench_latency(q, N_LATENCY, "SPSCQueue"); }
        { BoundedMPMCQueue<int, 64> q; bench_latency(q, N_LATENCY, "BoundedMPMCQueue"); }
        { SegmentedMPMCQueue<int, 64> q; bench_latency(q, N_LATENCY, "SegmentedMPMCQueue"); }
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    // ── Virtual dispatch overhead ────────────────────────────────────
    {
        Timer sec;
        std::cout << "── Virtual dispatch overhead ────────────────────\n";
        bench_virtual(OPS_VIRT, WARM_SEQ);
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    std::cout << "════════════════════════════════════════════════════════\n"
              << "  Total: " << total.elapsed_s() << " s\n"
              << "════════════════════════════════════════════════════════\n";
    return 0;
}
