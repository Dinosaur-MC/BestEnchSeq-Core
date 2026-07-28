// SPDX-License-Identifier: MIT
#include "ThreadPool.h"

#include <cassert>
#include <latch>
#include <stop_token>
#include <utility>

namespace besq {

// =======================================================================
// Construction
// =======================================================================

ThreadPool::ThreadPool(std::size_t num_threads, ThreadPoolMode mode)
    : _mode(mode) {

    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 2; // safety floor
    }

    _workers.reserve(num_threads);

    // Use a latch so the constructor blocks until all workers are running.
    // This ensures submit() calls immediately after construction see a
    // fully operational pool.
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
    {
        std::lock_guard<std::mutex> lock(_global_mtx);
        if (_closed) return;
        _closed = true;
    }
    // Request all workers to stop.
    for (auto& w : _workers) {
        w.request_stop();
    }
    _cv.notify_all();

    // std::jthread destructor joins automatically.  We explicitly join
    // here so that after stop() returns, no worker is still running.
    for (auto& w : _workers) {
        if (w.joinable()) w.join();
    }
}

// =======================================================================
// wait()
// =======================================================================

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(_global_mtx);
    _cv.wait(lock, [this] {
        return _pending.load(std::memory_order_acquire) == 0;
    });
}

// =======================================================================
// Internal: _enqueue
// =======================================================================

void ThreadPool::_enqueue(std::unique_ptr<TaskBase> task) {
    {
        std::lock_guard<std::mutex> lock(_global_mtx);
        if (_closed) {
            // task is destroyed here → packaged_task dtor sets broken_promise
            // on the associated future, so fut.get() throws future_error.
            return;
        }
        _global_queue.push_back(std::move(task));
        // Increment _pending UNDER THE LOCK so the count is visible by
        // the time any worker could pop this task (also done under the lock).
        _pending.fetch_add(1, std::memory_order_release);
    }

    _cv.notify_one();
}

// =======================================================================
// Internal: _try_pop_global
// =======================================================================

std::unique_ptr<ThreadPool::TaskBase> ThreadPool::_try_pop_global() {
    // Called from worker_main when it already holds _global_mtx.
    // (Separate method for clarity; the calling convention is documented
    // but not enforced by the type system.)
    if (_global_queue.empty()) return nullptr;

    auto task = std::move(_global_queue.front());
    _global_queue.pop_front();
    return task;
}

// =======================================================================
// Internal: _on_task_done
// =======================================================================

void ThreadPool::_on_task_done() noexcept {
    auto prev = _pending.fetch_sub(1, std::memory_order_acq_rel);
    assert(prev > 0 && "pending underflow");

    if (prev == 1) {
        // The last task just finished.  Wake up wait().
        // notify_all is needed even though there's only one "waiter"
        // because condition_variable::notify_one might wake a worker
        // instead of the waiter.  notify_all is simpler and correct.
        _cv.notify_all();
    }
    // else: more tasks remain — no need to poke the cv.
}

// =======================================================================
// Internal: _worker_main
// =======================================================================

void ThreadPool::_worker_main(std::size_t /*id*/, std::stop_token st) {
    // Register a stop callback that notifies the cv so we don't get stuck
    // waiting when the pool is shut down.
    std::stop_callback wake_on_stop(st, [this] {
        _cv.notify_all();
    });

    while (true) {
        std::unique_ptr<TaskBase> task;

        // ---- Phase 1: try global queue --------------------------------
        {
            std::lock_guard<std::mutex> lock(_global_mtx);
            task = _try_pop_global();
        }

        if (task) {
            task->run();
            _on_task_done();
            continue;
        }

        // ---- Phase 2: wait for work ----------------------------------
        std::unique_lock<std::mutex> lock(_global_mtx);

        // Re-check under the lock before going to sleep.
        task = _try_pop_global();
        if (task) {
            lock.unlock();
            task->run();
            _on_task_done();
            continue;
        }

        // Check for shutdown.
        if (st.stop_requested() && _global_queue.empty() && _pending.load(std::memory_order_acquire) == 0) {
            break;
        }

        // Sleep until something changes.
        _cv.wait(lock, [this, &st] {
            return _closed || !_global_queue.empty() || st.stop_requested();
        });

        // If the pool is closed and there's nothing left, exit.
        if (_closed && _global_queue.empty() && _pending.load(std::memory_order_acquire) == 0) {
            break;
        }
    }
}

// =======================================================================
// Shared pool singleton
// =======================================================================

ThreadPool& ThreadPool::shared() {
    static ThreadPool pool(std::thread::hardware_concurrency(),
                           ThreadPoolMode::SingleQueue);
    return pool;
}

} // namespace besq
