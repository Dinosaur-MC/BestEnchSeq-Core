#include "utils/EventLoop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
//  Configuration  —  calibrate for ~5 s total on a modern CPU
// ═══════════════════════════════════════════════════════════════════════════

constexpr int64_t OPS_EV     =  5'000'000;   // tasks posted per EventLoop test
constexpr int64_t WARM_EV    =    200'000;   // warm-up tasks
constexpr int64_t OPS_MP     =  5'000'000;   // tasks for multi-producer test
constexpr int64_t N_LATENCY  =     10'000;   // latency samples

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
    std::snprintf(buf, sizeof(buf), "  %-42s %8.2f M tasks/s  (%5.2f s)",
                  label, rate, elapsed_s);
    std::cout << buf << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
//  EventLoop single-producer throughput
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_throughput(EventLoop<Queue>& loop,
                             int64_t n, int64_t warmup,
                             const char* label)
{
    std::atomic<int64_t> sum{0};
    loop.start();

    for (int64_t i = 0; i < warmup; ++i) {
        while (!loop.post([&] { sum.fetch_add(1); })) {}
    }
    loop.post_and_wait([&] {});

    sum.store(0);
    auto t0 = Clock::now();
    for (int64_t i = 0; i < n; ++i) {
        while (!loop.post([&] { sum.fetch_add(1); })) {}
    }
    loop.post_and_wait([&] {});
    double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t0).count() / 1'000'000.0;

    print_result(label, sec, n);
    loop.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Multi-producer throughput  (N threads posting to the same loop)
// ═══════════════════════════════════════════════════════════════════════════

static void bench_multiproducer(int64_t n, int64_t warmup, int n_threads) {
    MPMCEventLoop<> loop;
    std::atomic<int64_t> sum{0};
    loop.start();

    int64_t per = warmup / n_threads;
    std::vector<std::thread> warmers;
    for (int t = 0; t < n_threads; ++t) {
        warmers.emplace_back([&] {
            for (int64_t i = 0; i < per; ++i)
                while (!loop.post([&] { sum.fetch_add(1); })) {}
        });
    }
    for (auto& t : warmers) t.join();
    loop.post_and_wait([&] {});

    sum.store(0);
    per = n / n_threads;
    auto t0 = Clock::now();
    std::vector<std::thread> producers;
    for (int t = 0; t < n_threads; ++t) {
        producers.emplace_back([&] {
            for (int64_t i = 0; i < per; ++i)
                while (!loop.post([&] { sum.fetch_add(1); })) {}
        });
    }
    for (auto& t : producers) t.join();
    loop.post_and_wait([&] {});
    double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t0).count() / 1'000'000.0;

    char buf[80];
    std::snprintf(buf, sizeof(buf), "MPMCEventLoop %dP post + exec", n_threads);
    print_result(buf, sec, n);
    loop.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  post_and_wait latency
// ═══════════════════════════════════════════════════════════════════════════

static void bench_latency(int64_t n) {
    MPMCEventLoop<> loop;
    loop.start();

    // Warm-up
    for (int64_t i = 0; i < 2000; ++i)
        loop.post_and_wait([] {});

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        auto t0 = Clock::now();
        loop.post_and_wait([] {});
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              t1 - t0).count());
    }

    std::sort(samples.begin(), samples.end());
    double median = samples[static_cast<size_t>(n) / 2];
    double mean   = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(n);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "  %-42s %6.1f ns median  (mean %6.1f ns)",
                  "MPMCEventLoop post_and_wait", median, mean);
    std::cout << buf << std::endl;
    loop.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    Timer total;

    std::cout << "╔══════════════════════════════════════════════════════════╗\n"
              << "║            EventLoop Benchmarks                          ║\n"
              << "╚══════════════════════════════════════════════════════════╝\n"
              << "  Tasks per test: " << OPS_EV / 1'000'000 << "M"
              << "   warm-up: " << WARM_EV / 1000 << "k\n"
              << "  Hardware concurrency: "
              << std::thread::hardware_concurrency() << "\n\n";

    // ── Single-producer throughput ────────────────────────────────────
    {
        Timer sec;
        std::cout << "── Single-producer throughput ────────────────────\n";
        {
            EventLoop<SegmentedMPMCQueue<std::function<void()>, 1024>> loop;
            bench_throughput(loop, OPS_EV, WARM_EV, "MPMCEventLoop (SegmentedMPMC)");
        }
        {
            EventLoop<SPSCQueue<std::function<void()>, 4096>> loop;
            bench_throughput(loop, OPS_EV, WARM_EV, "SPSCEventLoop (SPSCQueue)");
        }
        {
            EventLoop<BoundedMPMCQueue<std::function<void()>, 4096>> loop;
            bench_throughput(loop, OPS_EV, WARM_EV, "BoundedEventLoop (BoundedMPMC)");
        }
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    // ── Multi-producer throughput ─────────────────────────────────────
    {
        Timer sec;
        std::cout << "── Multi-producer throughput ─────────────────────\n";
        bench_multiproducer(OPS_MP, WARM_EV, 2);
        bench_multiproducer(OPS_MP, WARM_EV, 4);
        bench_multiproducer(OPS_MP, WARM_EV, 8);
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    // ── post_and_wait latency ─────────────────────────────────────────
    {
        Timer sec;
        std::cout << "── post_and_wait latency ─────────────────────────\n";
        bench_latency(N_LATENCY);
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    std::cout << "════════════════════════════════════════════════════════\n"
              << "  Total: " << total.elapsed_s() << " s\n"
              << "════════════════════════════════════════════════════════\n";
    return 0;
}
