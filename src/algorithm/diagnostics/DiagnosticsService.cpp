#include "algorithm/diagnostics/DiagnosticsService.h"
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

std::vector<std::shared_ptr<AlgorithmObserver>> DiagnosticsService::snapshot_observers() {
    std::lock_guard lock(_obs_mtx);
    return _observers;
}

// ─── DiagnosticsHandler ────────────────────────────────────────────────────

void DiagnosticsService::DiagnosticsHandler::operator()(DiagnosticsEvent event) {
    // 1. Snapshot observer list under lock, then dispatch outside lock
    std::vector<std::shared_ptr<AlgorithmObserver>> local;
    if (obs_mtx && observers) {
        std::lock_guard lock(*obs_mtx);
        local = *observers;
    }

    // 2. Handle based on event kind
    switch (event.kind) {
        case DiagEventKind::Exit: {
            auto& p = std::get<DiagnosticsEvent::ExitPayload>(event.payload);

            // Build all entries from all sources
            std::vector<DiagnosticsWriter::Entry> all;
            all.reserve(3 + p.flush_entries.size() + 10);

            // Add entries from diagnostics->flush() if available
            if (p.diagnostics) {
                p.diagnostics->flush(all);
            }

            // Add flush_entries (backward compat path from AlgorithmExecutor)
            for (auto& e : p.flush_entries)
                all.push_back(std::move(e));

            // Add atomic counter entries
            all.push_back(std::move(p.nodes_visited));
            all.push_back(std::move(p.nodes_pruned));
            all.push_back(std::move(p.steps_forged));

            // Persist to file
            if (persist && persist->load(std::memory_order_acquire))
                DiagnosticsWriter::write(event.algorithm_name, all,
                                         p.wall_ms, p.status);

            // Notify observers (diagnostic + completed)
            if (!local.empty()) {
                std::ostringstream oss;
                oss << "algorithm=" << event.algorithm_name
                    << " status=" << p.status
                    << " wall_ms=" << p.wall_ms;
                DiagnosticInfo info{oss.str()};

                for (auto& obs : local) {
                    obs->on_diagnostic(info);
                    obs->on_completed(p.output);
                }
            }
            break;
        }
        case DiagEventKind::Progress: {
            if (local.empty()) break;
            auto& p = std::get<DiagnosticsEvent::ProgressPayload>(event.payload);
            for (auto& obs : local)
                obs->on_progress(p.percent, p.status);
            break;
        }
        case DiagEventKind::Solution: {
            if (local.empty()) break;
            auto& p = std::get<DiagnosticsEvent::SolutionPayload>(event.payload);
            if (p.solution)
                for (auto& obs : local)
                    obs->on_solution_found(p.solution->steps);
            break;
        }
        case DiagEventKind::StateChange: {
            if (local.empty()) break;
            auto& p = std::get<DiagnosticsEvent::StatePayload>(event.payload);
            for (auto& obs : local)
                obs->on_state_changed(p.prev, p.curr);
            break;
        }
    }

    // 3. Signal completion for flush()
    if (processed_ptr)
        processed_ptr->fetch_add(1, std::memory_order_release);
}
