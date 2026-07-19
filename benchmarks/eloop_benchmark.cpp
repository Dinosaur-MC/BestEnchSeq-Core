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

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>

/// Run a test in a forked child process for heap/cache isolation.
static void fork_isolated(auto fn) {
    std::cout.flush();  // flush before fork so buffered banner isn't duplicated
    pid_t pid = fork();
    if (pid == 0) { fn(); _exit(0); }
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        std::cerr << "WARN: child exit status " << WEXITSTATUS(status) << '\n';
}
#else
/// Windows fallback — no isolation.
static void fork_isolated(auto fn) { fn(); }
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Configuration  —  calibrate for ~8-10 s total on a modern CPU
// ═══════════════════════════════════════════════════════════════════════════

#ifdef NDEBUG
constexpr int64_t OPS_EV     = 15'000'000;   // tasks posted per EventLoop test
constexpr int64_t WARM_EV    =    500'000;   // warm-up tasks
constexpr int64_t OPS_MP     = 15'000'000;   // tasks for multi-producer test
#else
constexpr int64_t OPS_EV     =  3'000'000;
constexpr int64_t WARM_EV    =    100'000;
constexpr int64_t OPS_MP     =  1'000'000;
#endif
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

/// Drain barrier: post a no-op task and wait for it to complete.
/// This ensures all previously posted tasks have been consumed.
static void drain(auto& loop) {
    std::atomic<bool> done{false};
    loop.post([&done] { done.store(true, std::memory_order_release); });
    while (!done.load(std::memory_order_acquire))
        std::this_thread::yield();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pipeline throughput  (producer thread + EventLoop consumer)
// ═══════════════════════════════════════════════════════════════════════════
//
// Unlike the raw queue benchmark (push-one/pop-one, single thread), the
// EventLoop benchmark must be a true pipeline: a producer thread posts
// tasks while the EventLoop worker consumes them concurrently.  This
// design is fair to both bounded and unbounded queues:
//
//   - Bounded:  producer naturally paces at consumer speed (back-pressure).
//               The capacity-*N* drain pattern is realistic — real code
//               never posts 15M items into a 4K buffer in one burst.
//   - Unbounded: producer runs freely, consumer drains in batches.
//               True producer-consumer parallelism is measured.
//
// The old "post all N then drain" pattern artificially penalised bounded
// queues: after the initial fill the producer spins on every single post,
// degrading to serialised stall measurement.

template <typename Loop>
static void bench_pipeline(Loop& loop,
                           int64_t n, int64_t warmup,
                           const char* label)
{
    std::atomic<int64_t> consumed{0};
    loop.start();

    // Warm-up: producer posts, consumer drains
    for (int64_t i = 0; i < warmup; ++i)
        loop.try_post([&] { consumed.fetch_add(1); });
    drain(loop);

    consumed.store(0);
    auto t0 = Clock::now();

    std::thread producer([&] {
        for (int64_t i = 0; i < n; ++i)
            loop.post([&] { consumed.fetch_add(1); });
    });

    // Main thread waits for all tasks to be consumed
    while (consumed.load(std::memory_order_acquire) < n)
        std::this_thread::yield();
    auto t1 = Clock::now();
    producer.join();

    double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                     t1 - t0).count() / 1'000'000.0;

    print_result(label, sec, n);
    loop.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Multi-producer throughput  (N threads posting to the same loop)
// ═══════════════════════════════════════════════════════════════════════════

template <typename Loop>
static void bench_multiproducer_tmpl(int64_t n, int64_t warmup, int n_threads,
                                     const char* label) {
    Loop loop;
    std::atomic<int64_t> sum{0};
    loop.start();

    int64_t per_warm = warmup / n_threads;
    std::vector<std::thread> warmers;
    for (int t = 0; t < n_threads; ++t) {
        warmers.emplace_back([&] {
            for (int64_t i = 0; i < per_warm; ++i)
                loop.post([&] { sum.fetch_add(1); });
        });
    }
    for (auto& t : warmers) t.join();
    drain(loop);

    sum.store(0);
    int64_t per = n / n_threads;
    auto t0 = Clock::now();
    std::vector<std::thread> producers;
    for (int t = 0; t < n_threads; ++t) {
        producers.emplace_back([&] {
            for (int64_t i = 0; i < per; ++i)
                loop.post([&] { sum.fetch_add(1); });
        });
    }
    for (auto& t : producers) t.join();
    drain(loop);
    double sec = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t0).count() / 1'000'000.0;

    char buf[80];
    std::snprintf(buf, sizeof(buf), "%s %dP post + exec", label, n_threads);
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
        drain(loop);

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        auto t0 = Clock::now();
        drain(loop);
        auto t1 = Clock::now();
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              t1 - t0).count());
    }

    std::sort(samples.begin(), samples.end());
    double median = samples[static_cast<size_t>(n) / 2];
    double mean   = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(n);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "  %-42s %6.1f ns median  (mean %6.1f ns)",
                  "MPMCEventLoop drain latency", median, mean);
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
    fork_isolated([] {
        Timer sec;
        std::cout << "── Single-producer throughput ────────────────────\n";
        {
            EventLoop<std::function<void()>, SegmentedMPMCQueue<std::function<void()>, 1024>> loop;
            bench_pipeline(loop, OPS_EV, WARM_EV, "MPMCEventLoop");
        }
        {
            EventLoop<std::function<void()>, SegmentedMPSCQueue<std::function<void()>>> loop;
            bench_pipeline(loop, OPS_EV, WARM_EV, "MPSCEventLoop");
        }
        {
            EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>> loop;
            bench_pipeline(loop, OPS_EV, WARM_EV, "BoundedMPMCEventLoop");
        }
        {
            EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>> loop;
            bench_pipeline(loop, OPS_EV, WARM_EV, "BoundedMPSCEventLoop");
        }
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    });

    // ── Multi-producer throughput ─────────────────────────────────────
    {
        Timer sec;
        std::cout << "── Multi-producer throughput ─────────────────────\n";
        fork_isolated([] {
            bench_multiproducer_tmpl<MPMCEventLoop<>>(OPS_MP, WARM_EV, 2, "MPMCEventLoop");
            bench_multiproducer_tmpl<MPSCEventLoop<>>(OPS_MP, WARM_EV, 2, "MPSCEventLoop");
            bench_multiproducer_tmpl<
                EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>>
            >(OPS_MP, WARM_EV, 2, "BoundedMPMCEventLoop");
            bench_multiproducer_tmpl<
                EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>>
            >(OPS_MP, WARM_EV, 2, "BoundedMPSCEventLoop");
        });
        fork_isolated([] {
            bench_multiproducer_tmpl<MPMCEventLoop<>>(OPS_MP, WARM_EV, 4, "MPMCEventLoop");
            bench_multiproducer_tmpl<MPSCEventLoop<>>(OPS_MP, WARM_EV, 4, "MPSCEventLoop");
            bench_multiproducer_tmpl<
                EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>>
            >(OPS_MP, WARM_EV, 4, "BoundedMPMCEventLoop");
            bench_multiproducer_tmpl<
                EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>>
            >(OPS_MP, WARM_EV, 4, "BoundedMPSCEventLoop");
        });
        fork_isolated([] {
            bench_multiproducer_tmpl<MPMCEventLoop<>>(OPS_MP, WARM_EV, 8, "MPMCEventLoop");
            bench_multiproducer_tmpl<MPSCEventLoop<>>(OPS_MP, WARM_EV, 8, "MPSCEventLoop");
            bench_multiproducer_tmpl<
                EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>>
            >(OPS_MP, WARM_EV, 8, "BoundedMPMCEventLoop");
            bench_multiproducer_tmpl<
                EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>>
            >(OPS_MP, WARM_EV, 8, "BoundedMPSCEventLoop");
        });
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    // ── drain latency ─────────────────────────────────────────
    {
        Timer sec;
        std::cout << "── drain latency ─────────────────────────\n";
        bench_latency(N_LATENCY);
        std::cout << "  ── section: " << sec.elapsed_s() << " s ──\n\n";
    }

    std::cout << "════════════════════════════════════════════════════════\n"
              << "  Total: " << total.elapsed_s() << " s\n"
              << "════════════════════════════════════════════════════════\n";
    return 0;
}
