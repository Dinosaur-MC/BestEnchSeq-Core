// ═══════════════════════════════════════════════════════════════════════════
// EventLoop benchmarks
//
// Migrated to the shared benchmark harness (benchmarks/framework/
// bench_framework.h): one BENCH_CASE is one measurement unit (a full
// pipeline run of n posts, one multi-producer burst, or a single
// post+wait drain round trip); the harness owns warmup / iterations /
// statistics / CLI (--list / --filter / --iterations / --warmup / --json).
//
// Run: build/bin/eloop_benchmark [options]
// ═══════════════════════════════════════════════════════════════════════════

#define BESQ_BENCH_MAIN
#include "framework/bench_framework.h"

#include "utils/EventLoop.hpp"
#include "utils/queue/SegmentedMPMCQueue.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

// ─── Configuration ─────────────────────────────────────────────────────────
#ifdef NDEBUG
constexpr int64_t OPS_EV = 15'000'000;   // tasks posted per EventLoop test
constexpr int64_t OPS_MP = 15'000'000;   // tasks for multi-producer test
#else
constexpr int64_t OPS_EV =  3'000'000;
constexpr int64_t OPS_MP =  1'000'000;
#endif

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
//
//  Unlike the raw queue benchmark (push-one/pop-one, single thread), the
//  EventLoop benchmark must be a true pipeline: a producer thread posts
//  tasks while the EventLoop worker consumes them concurrently.  This
//  design is fair to both bounded and unbounded queues:
//
//    - Bounded:  producer naturally paces at consumer speed (back-pressure).
//    - Unbounded: producer runs freely, consumer drains in batches.
//
//  One measurement unit: start the loop, post n tasks from a producer
//  thread, wait until all n are consumed, then stop the loop.
// ═══════════════════════════════════════════════════════════════════════════

template <typename Loop>
static void bench_pipeline(Loop& loop, int64_t n) {
    std::atomic<int64_t> consumed{0};
    loop.start();

    std::thread producer([&] {
        for (int64_t i = 0; i < n; ++i)
            loop.post([&] { consumed.fetch_add(1); });
    });

    // Main thread waits for all tasks to be consumed
    while (consumed.load(std::memory_order_acquire) < n)
        std::this_thread::yield();
    producer.join();

    loop.stop();
}

BENCH_CASE_GROUP("mpmc event loop pipeline", "pipeline", OPS_EV) {
    EventLoop<std::function<void()>, SegmentedMPMCQueue<std::function<void()>, 1024>> loop;
    bench_pipeline(loop, OPS_EV);
}

BENCH_CASE_GROUP("mpsc event loop pipeline", "pipeline", OPS_EV) {
    EventLoop<std::function<void()>, SegmentedMPSCQueue<std::function<void()>>> loop;
    bench_pipeline(loop, OPS_EV);
}

BENCH_CASE_GROUP("bounded-mpmc event loop pipeline", "pipeline", OPS_EV) {
    EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>> loop;
    bench_pipeline(loop, OPS_EV);
}

BENCH_CASE_GROUP("bounded-mpsc event loop pipeline", "pipeline", OPS_EV) {
    EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>> loop;
    bench_pipeline(loop, OPS_EV);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Multi-producer throughput  (N threads posting to the same loop)
//  One measurement unit: start, post n tasks from N producer threads,
//  join them, drain the queue, stop the loop.
// ═══════════════════════════════════════════════════════════════════════════

template <typename Loop>
static void bench_multiproducer(int64_t n, int n_threads) {
    Loop loop;
    std::atomic<int64_t> sum{0};
    loop.start();

    int64_t per = n / n_threads;
    std::vector<std::thread> producers;
    for (int t = 0; t < n_threads; ++t) {
        producers.emplace_back([&] {
            for (int64_t i = 0; i < per; ++i)
                loop.post([&] { sum.fetch_add(1); });
        });
    }
    for (auto& t : producers) t.join();
    drain(loop);
    loop.stop();
}

BENCH_CASE_GROUP("mpmc event loop 2P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, SegmentedMPMCQueue<std::function<void()>, 1024>>
    >(OPS_MP, 2);
}
BENCH_CASE_GROUP("mpsc event loop 2P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<MPSCEventLoop<>>(OPS_MP, 2);
}
BENCH_CASE_GROUP("bounded-mpmc event loop 2P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>>
    >(OPS_MP, 2);
}
BENCH_CASE_GROUP("bounded-mpsc event loop 2P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>>
    >(OPS_MP, 2);
}
BENCH_CASE_GROUP("mpmc event loop 4P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, SegmentedMPMCQueue<std::function<void()>, 1024>>
    >(OPS_MP, 4);
}
BENCH_CASE_GROUP("mpsc event loop 4P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<MPSCEventLoop<>>(OPS_MP, 4);
}
BENCH_CASE_GROUP("bounded-mpmc event loop 4P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>>
    >(OPS_MP, 4);
}
BENCH_CASE_GROUP("bounded-mpsc event loop 4P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>>
    >(OPS_MP, 4);
}
BENCH_CASE_GROUP("mpmc event loop 8P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, SegmentedMPMCQueue<std::function<void()>, 1024>>
    >(OPS_MP, 8);
}
BENCH_CASE_GROUP("mpsc event loop 8P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<MPSCEventLoop<>>(OPS_MP, 8);
}
BENCH_CASE_GROUP("bounded-mpmc event loop 8P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, BoundedMPMCQueue<std::function<void()>, 4096>>
    >(OPS_MP, 8);
}
BENCH_CASE_GROUP("bounded-mpsc event loop 8P post+exec", "multi-producer", OPS_MP) {
    bench_multiproducer<
        EventLoop<std::function<void()>, BoundedMPSCQueue<std::function<void()>, 4096>>
    >(OPS_MP, 8);
}

// ═══════════════════════════════════════════════════════════════════════════
//  post_and_wait (drain) latency
//  One measurement unit = one post+wait round trip; the harness turns the
//  per-round-trip latency distribution into median / p95 / best across
//  iterations (previously: 10k internal samples).  The loop is started once
//  by the setup hook and kept running across iterations.
// ═══════════════════════════════════════════════════════════════════════════

static MPMCEventLoop<> s_latency_loop;
static bool s_latency_started = false;

// BENCH_CASE_FULL has no group/ops parameters, so register the drain-latency
// case directly (same body/setup/teardown, plus group "drain latency" and
// ops = 1 post+wait round trip per iteration).
static void bench_drain_latency();
static const ::bench::Registrar bench_reg_drain_latency(
    "mpmc event loop drain latency", &bench_drain_latency, 0,
    "drain latency", 1, "",
    [] {
        if (!s_latency_started) {
            s_latency_loop.start();
            s_latency_started = true;
        }
    },
    [] {});
static void bench_drain_latency() {
    drain(s_latency_loop);
}
