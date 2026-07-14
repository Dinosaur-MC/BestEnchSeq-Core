#include "algorithm/diagnostics/DiagnosticsService.h"
#include <chrono>

// ─── Singleton (Meyer's) ────────────────────────────────────────────────────

DiagnosticsService& DiagnosticsService::instance() {
    static DiagnosticsService service;
    return service;
}

// ─── Constructor / Destructor ───────────────────────────────────────────────

DiagnosticsService::DiagnosticsService() {
    _worker_thread = std::thread(&DiagnosticsService::_worker, this);
}

DiagnosticsService::~DiagnosticsService() {
    _running.store(false, std::memory_order_release);
    _wake_seq.fetch_add(1, std::memory_order_release);
    _wake_seq.notify_all();
    if (_worker_thread.joinable())
        _worker_thread.join();
}

// ─── Public API ─────────────────────────────────────────────────────────────

void DiagnosticsService::push(DiagnosticsEvent event) {
    if (_queue.try_push(std::move(event))) {
        _wake_seq.fetch_add(1, std::memory_order_release);
        _wake_seq.notify_one();
    }
}

void DiagnosticsService::flush() {
    // Busy-wait: keep waking the worker until the queue is fully drained.
    while (!_queue.empty()) {
        _wake_seq.fetch_add(1, std::memory_order_release);
        _wake_seq.notify_one();
        std::this_thread::yield();
    }
}

// ─── Internal helpers ───────────────────────────────────────────────────────

void DiagnosticsService::_process_one(DiagnosticsEvent& event) {
    if (!_persist.load(std::memory_order_acquire))
        return;
    auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - event.timestamp).count();
    DiagnosticsWriter::write(
        event.algorithm_name,
        event.entries,
        wall_ms,
        event.status);
}

// ─── Worker thread ──────────────────────────────────────────────────────────

void DiagnosticsService::_worker() {
    DiagnosticsEvent event;
    while (_running.load(std::memory_order_acquire)) {
        // Drain all available events (burst handling).
        while (_queue.try_pop(event))
            _process_one(event);

        if (!_running.load(std::memory_order_acquire))
            break;

        // C++20 atomic::wait — zero CPU while idle, no mutex/CV.
        auto prev = _wake_seq.load(std::memory_order_acquire);
        if (_queue.try_pop(event)) {
            _process_one(event);
            continue;
        }
        _wake_seq.wait(prev, std::memory_order_acquire);
    }

    // Drain remaining events after shutdown signal.
    while (_queue.try_pop(event))
        _process_one(event);
}
