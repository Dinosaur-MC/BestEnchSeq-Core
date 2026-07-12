#include "utils/queue/SPSCQueue.hpp"
#include "utils/queue/BoundedMPMCQueue.hpp"
#include "utils/queue/SegmentedMPMCQueue.hpp"
#include "utils/queue/IQueue.h"
#include "utils/EventLoop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
//  Configuration
// ═══════════════════════════════════════════════════════════════════════════

constexpr int64_t OPS       = 100'000;    // push-pop pairs per sequential test
constexpr int64_t WARMUP    =  10'000;    // warm-up iterations
constexpr int64_t N_LATENCY =   1'000;    // latency-measurement samples

// ═══════════════════════════════════════════════════════════════════════════
//  Timer
// ═══════════════════════════════════════════════════════════════════════════

using Clock = std::chrono::high_resolution_clock;

struct Timestamp {
    Clock::time_point t;
    static Timestamp now() { return {Clock::now()}; }
    double elapsed_us(const Timestamp& other) const {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(other.t - t).count()) / 1000.0;
    }
    double elapsed_ns(const Timestamp& other) const {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(other.t - t).count());
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  API normalisation (mirrors EventLoop::consume)
// ═══════════════════════════════════════════════════════════════════════════

template <typename Q, typename T>
static bool push_item(Q& q, T&& val) {
    if constexpr (std::is_same_v<decltype(q.push(std::forward<T>(val))), bool>)
        return q.push(std::forward<T>(val));
    else {
        q.push(std::forward<T>(val));
        return true;
    }
}

template <typename Q, typename T>
static bool pop_item(Q& q, T& out) {
    if constexpr (requires { q.try_pop(out); })
        return q.try_pop(out);
    else
        return q.pop(out);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Results
// ═══════════════════════════════════════════════════════════════════════════

static std::vector<std::string> s_lines;

static void record(const std::string& line) {
    s_lines.push_back(line);
    std::cout << line << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Sequential push-pop pairs  (bounded queues never overfill because each
//  push is immediately followed by a pop before the next push.)
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_seq(Queue& q, int64_t n, const char* label) {
    int v;
    for (int64_t i = 0; i < WARMUP; ++i) {
        while (!push_item(q, static_cast<int>(i))) {}
        pop_item(q, v);
    }

    auto t0 = Timestamp::now();
    for (int64_t i = 0; i < n; ++i) {
        while (!push_item(q, static_cast<int>(i & 0x7FFFFFFF))) {}
        pop_item(q, v);
    }
    auto t1 = Timestamp::now();

    double ops = static_cast<double>(n) / (t0.elapsed_us(t1) / 1'000'000.0);
    char buf[80];
    std::snprintf(buf, sizeof(buf), "  %-40s %8.2f M pairs/s", label, ops);
    record(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Round-trip latency (single push-pop pair)
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_latency(Queue& q, int64_t n, const char* label) {
    int v;
    for (int i = 0; i < 1000; ++i) {
        while (!push_item(q, i)) {}
        pop_item(q, v);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(n));

    for (int64_t i = 0; i < n; ++i) {
        auto t0 = Timestamp::now();
        while (!push_item(q, 42)) {}
        pop_item(q, v);
        auto t1 = Timestamp::now();
        samples.push_back(t0.elapsed_ns(t1));
    }

    std::sort(samples.begin(), samples.end());
    double median = samples[samples.size() / 2];
    double mean   = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    char buf[80];
    std::snprintf(buf, sizeof(buf), "  %-40s %8.1f ns  (mean %.1f ns)",
                  label, median, mean);
    record(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
//  EventLoop throughput (single producer, retry on bounded full)
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_event_loop(EventLoop<Queue>& loop,
                              int64_t n, const char* label)
{
    std::atomic<int64_t> sum{0};
    loop.start();

    for (int64_t i = 0; i < WARMUP; ++i) {
        while (!loop.post([&] { sum.fetch_add(1); })) {}
    }
    loop.post_and_wait([&] {});

    sum.store(0);
    auto t0 = Timestamp::now();
    for (int64_t i = 0; i < n; ++i) {
        while (!loop.post([&] { sum.fetch_add(1); })) {}
    }
    loop.post_and_wait([&] {});
    auto t1 = Timestamp::now();

    double ops = static_cast<double>(n) / (t0.elapsed_us(t1) / 1'000'000.0);
    char buf[80];
    std::snprintf(buf, sizeof(buf), "  %-40s %8.2f M tasks/s", label, ops);
    record(buf);
    loop.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  IQueue virtual-dispatch overhead
// ═══════════════════════════════════════════════════════════════════════════

static void bench_virtual(int64_t n) {
    int v;
    // Baseline: direct SPSCQueue
    {
        SPSCQueue<int, 4096> q;
        for (int64_t i = 0; i < WARMUP; ++i) {
            while (!q.push(static_cast<int>(i))) {}
            q.pop(v);
        }
        auto t0 = Timestamp::now();
        for (int64_t i = 0; i < n; ++i) {
            while (!q.push(i)) {}
            q.pop(v);
        }
        auto t1 = Timestamp::now();
        double ops = static_cast<double>(n) / (t0.elapsed_us(t1) / 1'000'000.0);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "  %-40s %8.2f M pairs/s",
                      "SPSCQueue direct (baseline)", ops);
        record(buf);
    }

    // QueueAdaptor::underlying() — same as direct
    {
        QueueAdaptor<int, SPSCQueue<int, 4096>> adapted;
        for (int64_t i = 0; i < WARMUP; ++i) {
            while (!adapted.underlying().push(i)) {}
            adapted.underlying().pop(v);
        }
        auto t0 = Timestamp::now();
        for (int64_t i = 0; i < n; ++i) {
            while (!adapted.underlying().push(i)) {}
            adapted.underlying().pop(v);
        }
        auto t1 = Timestamp::now();
        double ops = static_cast<double>(n) / (t0.elapsed_us(t1) / 1'000'000.0);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "  %-40s %8.2f M pairs/s",
                      "QueueAdaptor::underlying()", ops);
        record(buf);
    }

    // Virtual dispatch through IQueue<T>
    {
        QueueAdaptor<int, SPSCQueue<int, 4096>> adapted;
        IQueue<int>& virt = adapted;
        for (int64_t i = 0; i < WARMUP; ++i) {
            while (!virt.try_push(static_cast<int>(i))) {}
            virt.try_pop(v);
        }
        auto t0 = Timestamp::now();
        for (int64_t i = 0; i < n; ++i) {
            while (!virt.try_push(static_cast<int>(i))) {}
            virt.try_pop(v);
        }
        auto t1 = Timestamp::now();
        double ops = static_cast<double>(n) / (t0.elapsed_us(t1) / 1'000'000.0);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "  %-40s %8.2f M pairs/s",
                      "IQueue virtual dispatch", ops);
        record(buf);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "BestEnchSeq-Core Queue & EventLoop Benchmarks\n"
              << "  " << OPS << " operations, " << WARMUP << " warm-up\n"
              << "  Hardware concurrency: " << std::thread::hardware_concurrency() << "\n\n";

    // ── Queue sequential push-pop pairs ──
    std::cout << "── Sequential throughput ──\n";
    { SPSCQueue<int, 4096> q; bench_seq(q, OPS, "SPSCQueue"); }
    { BoundedMPMCQueue<int, 4096> q; bench_seq(q, OPS, "BoundedMPMCQueue"); }
    { SegmentedMPMCQueue<int, 1024> q; bench_seq(q, OPS, "SegmentedMPMCQueue"); }

    // ── Queue round-trip latency ──
    std::cout << "── Round-trip latency ──\n";
    { SPSCQueue<int, 64> q; bench_latency(q, N_LATENCY, "SPSCQueue"); }
    { BoundedMPMCQueue<int, 64> q; bench_latency(q, N_LATENCY, "BoundedMPMCQueue"); }
    { SegmentedMPMCQueue<int, 64> q; bench_latency(q, N_LATENCY, "SegmentedMPMCQueue"); }

    // ── EventLoop throughput ──
    std::cout << "── EventLoop throughput ──\n";
    {
        EventLoop<SegmentedMPMCQueue<std::function<void()>, 1024>> loop;
        bench_event_loop(loop, OPS, "MPMCEventLoop (SegmentedMPMC)");
    }
    {
        EventLoop<SPSCQueue<std::function<void()>, 4096>> loop;
        bench_event_loop(loop, OPS, "SPSCEventLoop (SPSCQueue)");
    }

    // ── Virtual dispatch overhead ──
    std::cout << "── Virtual dispatch overhead ──\n";
    bench_virtual(OPS / 2);

    std::cout << "\nDone.\n";
    return 0;
}
