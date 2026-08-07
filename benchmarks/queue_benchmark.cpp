// ═══════════════════════════════════════════════════════════════════════════
// Lock-Free Queue benchmarks
//
// Migrated to the shared benchmark harness (benchmarks/framework/
// bench_framework.h): one BENCH_CASE is one measurement unit (a full
// push/pop batch, a single push+pop round trip, or one N-producer run);
// the harness owns warmup / iterations / statistics / CLI
// (--list / --filter / --iterations / --warmup / --json / --help).
//
// Run: build/bin/queue_benchmark [options]
// ═══════════════════════════════════════════════════════════════════════════

#define BESQ_BENCH_MAIN
#include "framework/bench_framework.h"

#include "utils/queue/SPSCQueue.hpp"
#include "utils/queue/BoundedMPMCQueue.hpp"
#include "utils/queue/SegmentedMPMCQueue.hpp"
#include "utils/queue/SegmentedMPSCQueue.hpp"
#include "utils/queue/BoundedMPSCQueue.hpp"
#include "utils/queue/IQueue.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

// ─── Configuration ─────────────────────────────────────────────────────────
// Operation counts — Release needs more work for stable timing.
// Segmented queues use a lower count: being unbounded they never
// free blocks until destruction (~12 KB per 1024 items × OPS).
#ifdef NDEBUG
constexpr int64_t OPS_SEQ  = 600'000'000;  // push-pop pairs (bounded queues)
constexpr int64_t OPS_SEG  =  60'000'000;  // Segmented*Queue (unbounded)
constexpr int64_t OPS_VIRT = 150'000'000;  // pairs for virtual-dispatch test
constexpr int64_t OPS_MP   =  60'000'000;  // items for multi-producer test
#else
constexpr int64_t OPS_SEQ  =   5'000'000;
constexpr int64_t OPS_SEG  =   1'000'000;
constexpr int64_t OPS_VIRT =   2'000'000;
constexpr int64_t OPS_MP   =   1'000'000;
#endif

// ─── Performance-sink helper ───────────────────────────────────────────────
// Accumulates checksum from popped values so the compiler cannot eliminate
// push/pop as dead code.  A volatile write at the end enforces the side
// effect across all optimisation levels.

struct Sink {
    int64_t val = 0;
    void add(int64_t v) noexcept { val ^= v; }
    void flush() noexcept { volatile int64_t _ = val; (void)_; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Sequential throughput  (push-pop pairs)
//  One measurement unit = one full n_pairs push/pop batch.  Every push is
//  matched by a pop, so the queue is empty again at the end of the unit.
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_seq(Queue& q, int64_t n_pairs) {
    Sink sink;
    int v;

    for (int64_t i = 0; i < n_pairs; ++i) {
        int val = static_cast<int>(i & 0x7FFFFFFF);
        while (!q.try_push(val)) {}
        q.try_pop(v);
        sink.add(v);
    }
    sink.flush();
}

BENCH_CASE("spsc push/pop") {
    static SPSCQueue<int, 4096> q;
    bench_seq(q, OPS_SEQ);
}

BENCH_CASE("bounded-mpmc push/pop") {
    static BoundedMPMCQueue<int, 4096> q;
    bench_seq(q, OPS_SEQ);
}

BENCH_CASE("segmented-mpmc push/pop") {
    static SegmentedMPMCQueue<int, 1024> q;
    bench_seq(q, OPS_SEG);
}

BENCH_CASE("bounded-mpsc push/pop") {
    static BoundedMPSCQueue<int, 4096> q;
    bench_seq(q, OPS_SEQ);
}

BENCH_CASE("segmented-mpsc push/pop") {
    static SegmentedMPSCQueue<int> q;
    bench_seq(q, OPS_SEG);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Round-trip latency
//  One measurement unit = a single push+pop round trip on an empty queue;
//  the harness turns the per-round-trip latency distribution into
//  median / p95 / best across iterations (previously: 10k internal samples
//  collected inside the benchmark).
// ═══════════════════════════════════════════════════════════════════════════

BENCH_CASE("spsc round-trip latency") {
    static SPSCQueue<int, 64> q;
    int v;
    while (!q.try_push(42)) {}
    q.try_pop(v);
}

BENCH_CASE("bounded-mpmc round-trip latency") {
    static BoundedMPMCQueue<int, 64> q;
    int v;
    while (!q.try_push(42)) {}
    q.try_pop(v);
}

BENCH_CASE("segmented-mpmc round-trip latency") {
    static SegmentedMPMCQueue<int, 64> q;
    int v;
    while (!q.try_push(42)) {}
    q.try_pop(v);
}

BENCH_CASE("bounded-mpsc round-trip latency") {
    static BoundedMPSCQueue<int, 64> q;
    int v;
    while (!q.try_push(42)) {}
    q.try_pop(v);
}

BENCH_CASE("segmented-mpsc round-trip latency") {
    static SegmentedMPSCQueue<int> q;
    int v;
    while (!q.try_push(42)) {}
    q.try_pop(v);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Multi-producer throughput  (N producers, 1 consumer)
//  One measurement unit = n items through the queue with N producer threads
//  plus one consumer thread (all threads created and joined inside the
//  unit; the queue is drained empty before the unit ends).
// ═══════════════════════════════════════════════════════════════════════════

template <typename Queue>
static void bench_concurrent_mp(int64_t n, int n_producers) {
    Queue q;

    std::atomic<int64_t> consumed{0};
    int64_t per = n / n_producers;

    std::vector<std::thread> producers;
    for (int t = 0; t < n_producers; ++t)
        producers.emplace_back([&, t] {
            int64_t base = static_cast<int64_t>(t) * 1'000'000;
            for (int64_t i = 0; i < per; ++i)
                // Spin-loop for bounded queues; no-op for unbounded
                while (!q.try_push(static_cast<int>(base + i))) {}
        });

    std::thread consumer([&] {
        Sink sink;
        while (consumed.load() < n) {
            int v;
            if (q.try_pop(v)) {
                consumed.fetch_add(1);
                sink.add(v);
            } else {
                std::this_thread::yield();
            }
        }
        // Drain any stragglers
        int v;
        while (q.try_pop(v)) { sink.add(v); }
        sink.flush();
    });

    for (auto& t : producers) t.join();
    consumer.join();
}

BENCH_CASE("bounded-mpmc 2-producer push/pop") {
    bench_concurrent_mp<BoundedMPMCQueue<int, 4096>>(OPS_MP, 2);
}
BENCH_CASE("segmented-mpmc 2-producer push/pop") {
    bench_concurrent_mp<SegmentedMPMCQueue<int, 1024>>(OPS_MP, 2);
}
BENCH_CASE("bounded-mpsc 2-producer push/pop") {
    bench_concurrent_mp<BoundedMPSCQueue<int, 4096>>(OPS_MP, 2);
}
BENCH_CASE("segmented-mpsc 2-producer push/pop") {
    bench_concurrent_mp<SegmentedMPSCQueue<int>>(OPS_MP, 2);
}
BENCH_CASE("bounded-mpmc 4-producer push/pop") {
    bench_concurrent_mp<BoundedMPMCQueue<int, 4096>>(OPS_MP, 4);
}
BENCH_CASE("segmented-mpmc 4-producer push/pop") {
    bench_concurrent_mp<SegmentedMPMCQueue<int, 1024>>(OPS_MP, 4);
}
BENCH_CASE("bounded-mpsc 4-producer push/pop") {
    bench_concurrent_mp<BoundedMPSCQueue<int, 4096>>(OPS_MP, 4);
}
BENCH_CASE("segmented-mpsc 4-producer push/pop") {
    bench_concurrent_mp<SegmentedMPSCQueue<int>>(OPS_MP, 4);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Virtual-dispatch overhead  (IQueue vs direct)
// ═══════════════════════════════════════════════════════════════════════════

BENCH_CASE("spsc direct push/pop (virtual baseline)") {
    static SPSCQueue<int, 4096> q;
    Sink sink;
    int v;
    for (int64_t i = 0; i < OPS_VIRT; ++i) {
        while (!q.try_push(static_cast<int>(i))) {}
        q.try_pop(v);
        sink.add(v);
    }
    sink.flush();
}

BENCH_CASE("spsc push/pop via QueueAdaptor::underlying()") {
    static QueueAdaptor<int, SPSCQueue<int, 4096>> adapted;
    Sink sink;
    int v;
    for (int64_t i = 0; i < OPS_VIRT; ++i) {
        while (!adapted.underlying().try_push(static_cast<int>(i))) {}
        adapted.underlying().try_pop(v);
        sink.add(v);
    }
    sink.flush();
}

BENCH_CASE("spsc push/pop via IQueue virtual dispatch") {
    static QueueAdaptor<int, SPSCQueue<int, 4096>> adapted;
    IQueue<int>& virt = adapted;
    Sink sink;
    int v;
    for (int64_t i = 0; i < OPS_VIRT; ++i) {
        while (!virt.try_push(static_cast<int>(i))) {}
        virt.try_pop(v);
        sink.add(v);
    }
    sink.flush();
}
