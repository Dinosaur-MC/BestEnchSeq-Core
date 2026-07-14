#include "algorithm/diagnostics/DiagnosticsService.h"
#include "log/log.hpp"
#include <algorithm>
#include <sstream>

// ─── Singleton (Meyer's) ────────────────────────────────────────────────────

DiagnosticsService& DiagnosticsService::instance() {
    static DiagnosticsService service;
    return service;
}

// ─── Constructor / Destructor ───────────────────────────────────────────────

DiagnosticsService::DiagnosticsService()
    : _loop(DiagnosticsHandler{&_obs_mtx, &_observers, &_persist, &_processed})
{
    _loop.start();
}

DiagnosticsService::~DiagnosticsService() {
    _loop.stop();  // graceful: drain remaining events
}

// ─── Public API ─────────────────────────────────────────────────────────────

void DiagnosticsService::push(DiagnosticsEvent event) {
    if (_loop.try_post(std::move(event))) {
        _enqueued.fetch_add(1, std::memory_order_release);
    } else {
        LOG_WARN("diagnostics queue full, event dropped (algo=%s)",
                 event.algorithm_name.c_str());
    }
}

void DiagnosticsService::attach_observer(std::shared_ptr<AlgorithmObserver> observer) {
    std::lock_guard lock(_obs_mtx);
    _observers.push_back(std::move(observer));
}

void DiagnosticsService::detach_observer(std::shared_ptr<AlgorithmObserver> observer) {
    std::lock_guard lock(_obs_mtx);
    auto it = std::find(_observers.begin(), _observers.end(), observer);
    if (it != _observers.end())
        _observers.erase(it);
}

void DiagnosticsService::flush() {
    auto target = _enqueued.load(std::memory_order_acquire);
    while (_processed.load(std::memory_order_acquire) < target)
        std::this_thread::yield();
}

void DiagnosticsService::set_persist(bool enabled) noexcept {
    _persist.store(enabled, std::memory_order_release);
}

// ─── DiagnosticsHandler ────────────────────────────────────────────────────

void DiagnosticsService::DiagnosticsHandler::operator()(DiagnosticsEvent event) {
    // 1. Snapshot observer list under lock, then dispatch outside lock
    std::vector<std::shared_ptr<AlgorithmObserver>> local;
    if (obs_mtx && observers) {
        std::lock_guard lock(*obs_mtx);
        local = *observers;
    }

    if (!local.empty()) {
        std::ostringstream oss;
        oss << "algorithm=" << event.algorithm_name
            << " status=" << event.status
            << " wall_ms=" << event.wall_ms;
        DiagnosticInfo info{oss.str()};

        for (auto& obs : local)
            obs->on_diagnostic(info);
    }

    // 2. Persist to file
    if (persist && persist->load(std::memory_order_acquire)) {
        DiagnosticsWriter::write(
            event.algorithm_name,
            event.entries,
            event.wall_ms,
            event.status);
    }

    // 3. Signal completion for flush()
    if (processed_ptr)
        processed_ptr->fetch_add(1, std::memory_order_release);
}
