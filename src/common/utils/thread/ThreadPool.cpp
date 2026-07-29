// SPDX-License-Identifier: MIT
#include "ThreadPool.h"

#include "utils/queue/IQueue.h"   // BESQ_PAUSE

#include <cassert>
#include <latch>
#include <stop_token>

namespace besq {

// =======================================================================
// Constants
// =======================================================================

/// How many spin+PAUSE iterations before a worker falls asleep.
static constexpr int SPIN_BEFORE_SLEEP = 64;

// =======================================================================
// Construction
// =======================================================================

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 2;
    }

    _workers.reserve(num_threads);

    std::latch ready(static_cast<std::ptrdiff_t>(num_threads));

    for (std::size_t i = 0; i < num_threads; ++i) {
        _workers.emplace_back([this, i, &ready](std::stop_token st) {
            ready.count_down();
            _worker_main(i, st);
        });
    }

    ready.wait();
}

// =======================================================================
// stop()
// =======================================================================

void ThreadPool::stop() noexcept {
    _stopped.store(true, std::memory_order_release);

    for (auto& w : _workers) w.request_stop();

    // Wake all workers blocked in _wake_counter.wait().
    _wake_counter.fetch_add(1, std::memory_order_release);
    _wake_counter.notify_all();

    for (auto& w : _workers) {
        if (w.joinable()) w.join();
    }

    // Drain any task that snuck in after the last worker exited (TOCTOU
    // between the worker's _pending==0 check and _enqueue's _stopped
    // check).  Without this, a leaked task with _pending>0 would cause
    // wait() to spin forever on a future shared_pool submit.
    TaskPtr leftover;
    while (_queue.try_pop(leftover)) {}
    _pending.store(0, std::memory_order_release);
}

// =======================================================================
// wait()
// =======================================================================

void ThreadPool::wait() {
    while (_pending.load(std::memory_order_acquire) > 0) {
        auto w = _wake_counter.load(std::memory_order_acquire);
        if (_pending.load(std::memory_order_acquire) == 0) break;
        _wake_counter.wait(w);
    }
}

// =======================================================================
// Internal: _enqueue
// =======================================================================

void ThreadPool::_enqueue(TaskPtr task) {
    // Check stopped BEFORE incrementing _pending.  Doing it the other
    // way around creates a window where _pending is >0 after workers
    // have already decided to exit (they saw _pending==0), leaving the
    // task orphaned and wait() hung.
    if (_stopped.load(std::memory_order_acquire)) {
        // task destroyed → packaged_task dtor sets broken_promise
        return;
    }

    auto prev = _pending.fetch_add(1, std::memory_order_release);

    _queue.try_push(std::move(task));   // always succeeds (unbounded)

    // Wake all idle workers.  With the fetch_add-based consumer path,
    // multi-consumer contention on dequeue_pos_ is minimal — no CAS
    // storm.  Workers pop tasks and execute them in parallel.
    if (prev == 0) {
        _wake_counter.fetch_add(1, std::memory_order_release);
        _wake_counter.notify_all();
    }
}

// =======================================================================
// Internal: _on_task_done
// =======================================================================

void ThreadPool::_on_task_done() noexcept {
    auto prev = _pending.fetch_sub(1, std::memory_order_acq_rel);
    assert(prev > 0 && "pending underflow");

    if (prev == 1) {
        // Last pending task just completed.  Wake wait().
        _wake_counter.fetch_add(1, std::memory_order_release);
        _wake_counter.notify_all();
    }
    // else: more tasks remain.  Workers that are already running find
    // new tasks in their Phase-1 hot loop — no notification needed.
    // (All workers were woken at once by the idle→active notify_all
    // in _enqueue, so cascade wakeups are redundant.)
}

// =======================================================================
// Internal: _worker_main
// =======================================================================

void ThreadPool::_worker_main(std::size_t /*id*/, std::stop_token st) {
    while (true) {
        TaskPtr task;

        // ---- Phase 1: lock-free pop (hot path) -------------------------
        phase1:
        if (_queue.try_pop(task)) [[likely]] {
            task->run();
            _on_task_done();
            continue;
        }

        // ---- Phase 2: check for shutdown -------------------------------
        if (st.stop_requested() &&
            _pending.load(std::memory_order_acquire) == 0) {
            break;
        }

        // ---- Phase 3: brief spin before sleep --------------------------
        for (int i = 0; i < SPIN_BEFORE_SLEEP; ++i) {
            if (_queue.try_pop(task)) {
                task->run();
                _on_task_done();
                goto phase1;     // back to top, NOT for-loop continue
            }
            BESQ_PAUSE();
        }

        // ---- Phase 4: sleep until _wake_counter changes ----------------
        {
            if (_queue.try_pop(task)) { task->run(); _on_task_done(); continue; }

            if (st.stop_requested() &&
                _pending.load(std::memory_order_acquire) == 0) {
                break;
            }

            if (_pending.load(std::memory_order_acquire) > 0) continue;

            // Secondary short spin: gives the submitter time to enqueue
            // the next task before we pay a full futex sleep→wake cycle.
            for (int i = 0; i < 16; ++i) {
                if (_pending.load(std::memory_order_acquire) > 0) goto phase1;
                BESQ_PAUSE();
            }

            auto w = _wake_counter.load(std::memory_order_acquire);
            _wake_counter.wait(w);
        }
    }
}

// =======================================================================
// Shared pool singleton
// =======================================================================

ThreadPool& ThreadPool::shared() {
    static ThreadPool pool(std::thread::hardware_concurrency());
    return pool;
}

} // namespace besq
