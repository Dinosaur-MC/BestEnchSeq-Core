#pragma once
#include "AlgorithmObserver.h"
#include "DiagnosticsEvent.h"
#include "common/utils/EventLoop.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace algorithm {

/// Global singleton for async persistence and observer dispatch.
class DiagnosticsService {
  public:
    static DiagnosticsService &instance();
    ~DiagnosticsService();

    DiagnosticsService(const DiagnosticsService &)            = delete;
    DiagnosticsService &operator=(const DiagnosticsService &) = delete;

    /// Enqueue a diagnostics event for async processing (non-blocking).
    /// Template auto-adapts between move and emplace based on argument count.
    template <typename... Args> void push(Args &&...args) {
        bool ok;
        if constexpr (
            sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, DiagnosticsEvent> && ...)
        ) {
            ok = _loop.try_post(std::forward<Args>(args)...);
        } else {
            ok = _loop.try_post_emplace(std::forward<Args>(args)...);
        }
        if (ok) [[likely]] {
            _enqueued.fetch_add(1, std::memory_order_release);
        } else {
            // Single-arg move path: extract name from the event for the warning.
            // Multi-arg emplace path: warn generically (name unavailable).
            if constexpr (
                sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, DiagnosticsEvent> && ...)
            ) {
                [&](auto &&ev) { _on_push_failed(ev.algorithm_name.c_str()); }(std::forward<Args>(args)...);
            } else {
                _on_push_failed(nullptr);
            }
        }
    }

    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);

    /// Busy-wait until all queued events have been processed.
    void flush();

    void set_persist(bool enabled) noexcept;

  private:
    /// Snapshot the current observer list (thread-safe).
    /// Only called internally by the diagnostics handler.
    std::vector<std::shared_ptr<AlgorithmObserver>> snapshot_observers();
    /// Called from push() when the queue is full (non-blocking path).
    /// Implemented in .cpp to keep log include out of the header.
    void _on_push_failed(const char *algo_name);

    /// Consumes DiagnosticsEvent instances on the EventLoop worker thread.
    /// Holds non-owning pointers to observer state in DiagnosticsService.
    struct DiagnosticsHandler {
        void operator()(DiagnosticsEvent event);

        std::mutex *obs_mtx{nullptr};
        std::vector<std::shared_ptr<AlgorithmObserver>> *observers{nullptr};
        std::atomic<bool> *persist{nullptr};
        std::atomic<uint64_t> *processed_ptr{nullptr};
    };

    DiagnosticsService();

    // ── Constructed first — pointed to by DiagnosticsHandler at _loop init ──
    std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;
    std::atomic<bool> _persist{true};
    std::atomic<uint64_t> _enqueued{0};
    std::atomic<uint64_t> _processed{0};

    // EventLoop (stores DiagnosticsHandler internally via move).
    EventLoop<DiagnosticsEvent, SegmentedMPSCQueue<DiagnosticsEvent>, DiagnosticsHandler> _loop;
};

} // namespace algorithm
