#pragma once
#include "algorithm/diagnostics/AlgorithmObserver.h"
#include "algorithm/diagnostics/DiagnosticsWriter.h"
#include "utils/queue/BoundedMPMCQueue.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// A single diagnostics event queued for async persistence + observer dispatch.
///
/// Created by any thread calling DiagnosticsService::push() and consumed
/// by the dedicated worker thread.
struct DiagnosticsEvent {
    std::string algorithm_name;
    std::vector<DiagnosticsWriter::Entry> entries;
    std::string status;
    int64_t wall_ms{0};
    std::chrono::system_clock::time_point timestamp;
};

/// Global singleton for async persistence and observer dispatch of
/// algorithm diagnostics.
///
/// Thread-safe push() via lock-free MPMC queue.  The worker thread blocks
/// on C++20 atomic::wait when idle — zero CPU, no mutex, no condition
/// variable.  Same pattern as Logger (see log/Logger.hpp).
///
/// Usage:
///   DiagnosticsService::instance().push(DiagnosticsEvent{...});
///   DiagnosticsService::instance().flush();  // drain before exit
class DiagnosticsService {
public:
    static DiagnosticsService& instance();
    ~DiagnosticsService();

    DiagnosticsService(const DiagnosticsService&) = delete;
    DiagnosticsService& operator=(const DiagnosticsService&) = delete;

    /// Enqueue a diagnostics event for async processing (non-blocking).
    /// Silently drops when the queue is full (QUEUE_CAPACITY = 64).
    void push(DiagnosticsEvent event);

    /// Attach a global observer (receives events for ALL algorithm runs).
    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);

    /// Detach a previously attached observer.
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);

    /// Busy-wait until all queued events have been written to disk.
    /// Intended for use at process exit (e.g. before main() returns).
    void flush();

    /// Enable/disable file persistence (default: enabled).
    void set_persist(bool enabled) noexcept { _persist.store(enabled, std::memory_order_release); }

private:
    DiagnosticsService();
    void _process_one(const DiagnosticsEvent& event);
    void _worker();

    static constexpr size_t QUEUE_CAPACITY = 64;

    BoundedMPMCQueue<DiagnosticsEvent, QUEUE_CAPACITY> _queue;
    std::atomic<bool>      _running{true};
    std::atomic<uint64_t>  _wake_seq{0};
    std::atomic<bool>      _persist{true};

    mutable std::mutex   _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;

    std::thread _worker_thread;
};
