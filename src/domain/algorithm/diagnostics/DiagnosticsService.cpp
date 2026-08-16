#include "IAlgorithmObserver.h"
#include "DiagnosticsService.h"
#include "common/log/log.hpp"
#include <algorithm>
#include <sstream>
namespace algorithm {

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

void DiagnosticsService::attach_observer(std::shared_ptr<IAlgorithmObserver> observer) {
    std::lock_guard lock(_obs_mtx);
    observer->_attached = true;
    _observers.push_back(std::move(observer));
}

void DiagnosticsService::detach_observer(std::shared_ptr<IAlgorithmObserver> observer) {
    std::lock_guard lock(_obs_mtx);
    auto it = std::find(_observers.begin(), _observers.end(), observer);
    if (it != _observers.end()) {
        (*it)->_attached = false;
        _observers.erase(it);
    }
}

void DiagnosticsService::flush() {
    auto target = _enqueued.load(std::memory_order_acquire);
    while (_processed.load(std::memory_order_acquire) < target)
        std::this_thread::yield();
}

void DiagnosticsService::set_persist(bool enabled) noexcept {
    _persist.store(enabled, std::memory_order_release);
}

void DiagnosticsService::_on_push_failed(const char* algo_name) {
    if (algo_name)
        LOG_WARN_ASYNC("diagnostics queue full, event dropped (algo=%s)", algo_name);
    else
        LOG_WARN_ASYNC("diagnostics queue full, event dropped");
}

std::vector<std::shared_ptr<IAlgorithmObserver>> DiagnosticsService::snapshot_observers() {
    std::lock_guard lock(_obs_mtx);
    return _observers;
}

// ─── DiagnosticsHandler ────────────────────────────────────────────────────

void DiagnosticsService::DiagnosticsHandler::operator()(DiagnosticsEvent event) {
    // 1. Snapshot observer list under lock, then dispatch outside lock
    std::vector<std::shared_ptr<IAlgorithmObserver>> local;
    if (obs_mtx && observers) {
        std::lock_guard lock(*obs_mtx);
        local = *observers;
    }

    // 2. Handle based on event kind
    switch (event.kind) {
        case DiagEventKind::Exit: {
            auto& p = std::get<DiagnosticsEvent::ExitPayload>(event.payload);

            // Notify observers FIRST — the entry moves below would leave the
            // payload's counter entries moved-from, and on_exit reads the
            // original counters/entries (web layer: exit structured KV).
            if (!local.empty()) {
                std::ostringstream oss;
                oss << "algorithm=" << event.algorithm_name
                    << " status=" << p.status
                    << " wall_ms=" << p.wall_ms;
                DiagnosticInfo info{oss.str()};

                for (auto& obs : local) {
                    if (!obs->accept_task_id(event.task_id)) continue;
                    obs->on_exit(event.task_id, event.algorithm_name, p);
                    obs->on_diagnostic(event.task_id, info);
                    obs->on_completed(event.task_id, p.output);
                }
            }

            // Build all entries from all sources
            std::vector<DiagnosticsWriter::Entry> all;
            all.reserve(3 + 20);

            // Add entries from diagnostics->flush() if available
            if (p.diagnostics) {
                p.diagnostics->flush(all);
                // Remove "status" entry — DiagnosticsWriter::write() already
                // writes status= from its own parameter, avoiding duplicates.
                all.erase(std::remove_if(all.begin(), all.end(),
                    [](const auto& e) { return e.key == std::string_view("status"); }),
                    all.end());
            }

            // Add atomic counter entries (incr_* counters).  These are Tier-2
            // per-operation counters: compiled to no-ops unless
            // BESQ_DEEP_DIAGNOSTICS, and unused by non-search strategies (DP,
            // deterministic).  When all three are zero they carry no
            // information — omit the whole set instead of emitting noise.
            const int64_t* n_visited = std::get_if<int64_t>(&p.nodes_visited.value);
            const int64_t* n_pruned  = std::get_if<int64_t>(&p.nodes_pruned.value);
            const int64_t* n_forged  = std::get_if<int64_t>(&p.steps_forged.value);
            if (!n_visited || !n_pruned || !n_forged ||
                *n_visited != 0 || *n_pruned != 0 || *n_forged != 0) {
                all.push_back(std::move(p.nodes_visited));
                all.push_back(std::move(p.nodes_pruned));
                all.push_back(std::move(p.steps_forged));
            }

            // Persist to file
            if (persist && persist->load(std::memory_order_acquire))
                DiagnosticsWriter::write(event.algorithm_name, all,
                                         p.wall_ms, p.status);
            break;
        }
        case DiagEventKind::Progress: {
            if (local.empty()) break;
            auto& p = std::get<DiagnosticsEvent::ProgressPayload>(event.payload);
            for (auto& obs : local) {
                if (!obs->accept_task_id(event.task_id)) continue;
                obs->on_progress(event.task_id, p.pct, p.status);
            }
            break;
        }
        case DiagEventKind::Solution: {
            if (local.empty()) break;
            auto& p = std::get<DiagnosticsEvent::SolutionPayload>(event.payload);
            if (p.solution)
                for (auto& obs : local) {
                    if (!obs->accept_task_id(event.task_id)) continue;
                    obs->on_solution_found(event.task_id, p.solution->steps);
                }
            break;
        }
        case DiagEventKind::StateChange: {
            if (local.empty()) break;
            auto& p = std::get<DiagnosticsEvent::StatePayload>(event.payload);
            for (auto& obs : local) {
                if (!obs->accept_task_id(event.task_id)) continue;
                obs->on_state_changed(event.task_id, p.prev, p.curr);
            }
            break;
        }
    }

    // 3. Signal completion for flush()
    if (processed_ptr)
        processed_ptr->fetch_add(1, std::memory_order_release);
}

} // namespace algorithm
