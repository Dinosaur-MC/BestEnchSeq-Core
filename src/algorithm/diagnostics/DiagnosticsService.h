#pragma once
#include "algorithm/diagnostics/AlgorithmObserver.h"
#include "algorithm/diagnostics/DiagnosticsEvent.h"
#include "utils/EventLoop.hpp"
#include "utils/queue/BoundedMPMCQueue.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// Global singleton for async persistence and observer dispatch.
class DiagnosticsService {
public:
    static DiagnosticsService& instance();
    ~DiagnosticsService();

    DiagnosticsService(const DiagnosticsService&) = delete;
    DiagnosticsService& operator=(const DiagnosticsService&) = delete;

    /// Enqueue a diagnostics event for async processing (non-blocking).
    void push(DiagnosticsEvent event);

    void attach_observer(std::shared_ptr<AlgorithmObserver> observer);
    void detach_observer(std::shared_ptr<AlgorithmObserver> observer);

    /// Busy-wait until all queued events have been processed.
    void flush();

    void set_persist(bool enabled) noexcept;

private:
    /// Consumes DiagnosticsEvent instances on the EventLoop worker thread.
    /// Holds non-owning pointers to observer state in DiagnosticsService.
    struct DiagnosticsHandler {
        void operator()(DiagnosticsEvent event);

        std::mutex* obs_mtx{nullptr};
        std::vector<std::shared_ptr<AlgorithmObserver>>* observers{nullptr};
        std::atomic<bool>* persist{nullptr};
        std::atomic<uint64_t>* processed_ptr{nullptr};
    };

    DiagnosticsService();

    static constexpr size_t QUEUE_CAPACITY = 64;

    // ── Constructed first — pointed to by DiagnosticsHandler at _loop init ──
    std::mutex _obs_mtx;
    std::vector<std::shared_ptr<AlgorithmObserver>> _observers;
    std::atomic<bool> _persist{true};
    std::atomic<uint64_t> _enqueued{0};
    std::atomic<uint64_t> _processed{0};

    // EventLoop (stores DiagnosticsHandler internally via move).
    EventLoop<DiagnosticsEvent,
              BoundedMPMCQueue<DiagnosticsEvent, QUEUE_CAPACITY>,
              DiagnosticsHandler> _loop;
};
