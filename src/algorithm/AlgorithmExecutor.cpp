#include "AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include <stdexcept>
#include <utility>

// ─── Construction / Destruction ───

AlgorithmExecutor::AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm)
    : _algorithm(std::move(algorithm))
{
    if (!_algorithm)
        throw std::invalid_argument("algorithm cannot be null");
}

AlgorithmExecutor::~AlgorithmExecutor() {
    cancel();
    _join_worker();
}

// ─── Internal helpers ───

void AlgorithmExecutor::_join_worker() noexcept {
    if (_worker && _worker->joinable())
        _worker->join();
}

void AlgorithmExecutor::_set_state(AlgorithmState new_state) noexcept {
    auto prev = _state.load(std::memory_order_acquire);
    while (prev != AlgorithmState::Cancelled && prev != AlgorithmState::Failed) {
        if (_state.compare_exchange_weak(prev, new_state,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            _state_cv.notify_all();

            // Notify observer (async, non-blocking)
            if (!_algo_name_cache.empty()) {
                DiagnosticsService::instance().push(
                    DiagEventKind::StateChange,
                    _algo_name_cache,
                    _task_id,
                    DiagnosticsEvent::StatePayload{prev, new_state});
            }
            return;
        }
    }
}

void AlgorithmExecutor::_run_warmup(AlgorithmInput& input, IAlgorithm& warmup_algo) {
    std::string warmup_name(warmup_algo.name());
    ExecutionContext warmup_ctx(0, warmup_name.c_str());
    warmup_algo.execute(input, warmup_ctx);
    auto solutions = warmup_ctx.get_solutions();
    if (!solutions.empty()) {
        int32_t bound = solutions[0].total_cost;
        for (const auto& sol : solutions)
            if (sol.total_cost < bound) bound = sol.total_cost;
        if (bound < input.initial_bound)
            input.initial_bound = bound;
    }
}

void AlgorithmExecutor::_finalize() {
    if (_finalized) return;
    _finalized = true;

    _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _start_time);

    auto atoms = _ctx->get_diagnostics(_computation_time.count());
    auto diag = _ctx->consume_exit_diagnostics();

    // Default if algorithm didn't set diagnostics
    if (!diag) {
        diag = std::make_unique<AlgorithmDiagnostics>();
        diag->algorithm_name = _algo_name_cache;
    }

    // Determine final status. Executor state overrides algorithm's status
    // for Cancelled/Failed.
    auto s = _state.load(std::memory_order_acquire);
    std::string status;
    if (s == AlgorithmState::Cancelled) {
        status = "Cancelled";
        diag->status = "Cancelled";
    } else if (s == AlgorithmState::Failed) {
        status = "Failed";
        diag->status = "Failed";
    } else {
        status = diag->status;
        if (status.empty()) status = "Complete";
    }

    DiagnosticsService::instance().push(
        DiagEventKind::Exit,
        _algo_name_cache,
        _task_id,
        DiagnosticsEvent::ExitPayload{
            std::move(diag),
            output(),
            status,
            _computation_time.count(),
            DiagnosticsWriter::Entry("nodes_visited", atoms.nodes_visited),
            DiagnosticsWriter::Entry("nodes_pruned",  atoms.nodes_pruned),
            DiagnosticsWriter::Entry("steps_forged",  atoms.steps_forged)
        });
}

// ─── Lifecycle ───

void AlgorithmExecutor::start(AlgorithmInput input,
                               std::unique_ptr<IAlgorithm> warmup) {
    AlgorithmState expected = AlgorithmState::Idle;
    if (!_state.compare_exchange_strong(expected, AlgorithmState::Running))
        throw std::logic_error("executor already running");

    _task_id = _next_task_id.fetch_add(1, std::memory_order_relaxed);
    _algo_name_cache = std::string(_algorithm->name());
    _ctx = std::make_unique<ExecutionContext>(_task_id, _algo_name_cache.c_str());
    _start_time = std::chrono::steady_clock::now();

    // Warmup phase (synchronous): run a fast algorithm to tighten bound
    if (warmup)
        _run_warmup(input, *warmup);
    
    // Main phase (asynchronous): run the actual algorithm
    _worker.emplace([this, input = std::move(input)]() mutable {
        try {
            _algorithm->execute(input, *_ctx);

            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);

            // Always attempt Completed; if cancel() already exchanged to
            // Cancelled, _set_state is a no-op — no TOCTOU race.
            _set_state(AlgorithmState::Completed);
        } catch (...) {
            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);
            _set_state(AlgorithmState::Failed);
        }
    });
}

void AlgorithmExecutor::start(AlgorithmInput input, const std::vector<uint8_t>& previous_state) {
    if (!previous_state.empty() && _algorithm->is_serializable())
        _algorithm->deserialize_state(previous_state);
    start(std::move(input));
}

void AlgorithmExecutor::pause() {
    AlgorithmState expected = AlgorithmState::Running;
    if (_state.compare_exchange_strong(expected, AlgorithmState::Paused))
        _ctx->pause();
}

void AlgorithmExecutor::resume() {
    AlgorithmState expected = AlgorithmState::Paused;
    if (_state.compare_exchange_strong(expected, AlgorithmState::Running)) {
        _ctx->resume();
    }
}

void AlgorithmExecutor::cancel() {
    AlgorithmState prev = _state.exchange(AlgorithmState::Cancelled, std::memory_order_acq_rel);
    // Don't clobber Completed or Failed — results/loss would be lost/mislabeled
    if (prev == AlgorithmState::Completed || prev == AlgorithmState::Failed) {
        _state.store(prev, std::memory_order_release);
        return;
    }
    if (prev == AlgorithmState::Running || prev == AlgorithmState::Paused) {
        if (_ctx) {
            _ctx->cancel();
            _ctx->resume();
        }
    }
    _state_cv.notify_all();

    // Notify observer of the Running/Paused → Cancelled transition
    if (!_algo_name_cache.empty())
        DiagnosticsService::instance().push(
            DiagEventKind::StateChange, _algo_name_cache, _task_id,
            DiagnosticsEvent::StatePayload{prev, AlgorithmState::Cancelled});
}

AlgorithmState AlgorithmExecutor::wait() {
    _join_worker();
    _finalize();
    auto s = _state.load(std::memory_order_acquire);
    return s;
}

AlgorithmState AlgorithmExecutor::wait_for(std::chrono::milliseconds timeout) {
    if (!_worker || !_worker->joinable())
        return _state.load(std::memory_order_acquire);

    std::unique_lock lock(_state_mtx);
    _state_cv.wait_for(lock, timeout, [this] {
        auto s = _state.load(std::memory_order_acquire);
        return s == AlgorithmState::Completed ||
               s == AlgorithmState::Failed ||
               s == AlgorithmState::Cancelled;
    });

    auto s = _state.load(std::memory_order_acquire);
    if (s == AlgorithmState::Completed ||
        s == AlgorithmState::Failed ||
        s == AlgorithmState::Cancelled) {
        _join_worker();
        _finalize();
    }
    return s;
}

AlgorithmState AlgorithmExecutor::state() const noexcept {
    return _state.load(std::memory_order_acquire);
}

double AlgorithmExecutor::progress() const noexcept {
    return _ctx ? _ctx->progress() : 0.0;
}

AlgorithmOutput AlgorithmExecutor::output() const {
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Completed)
        return AlgorithmOutput{}; // is_valid defaults to false

    AlgorithmOutput out;
    out.task_id = _task_id;
    out.algorithm_name = std::string(_algorithm->name());
    out.algorithm_version = std::string(_algorithm->version());
    out.created_at = std::chrono::system_clock::now();
    out.computation_time = _computation_time;
    out.solutions = _ctx->get_solutions();
    out.is_valid = true;
    return out;
}

std::vector<uint8_t> AlgorithmExecutor::serialize_state() const {
    auto s = _state.load(std::memory_order_acquire);
    if (s != AlgorithmState::Paused)
        return {};
    if (!_algorithm->is_serializable())
        return {};
    return _algorithm->serialize_state();
}

bool AlgorithmExecutor::restore_state(const std::vector<uint8_t>& data) {
    auto s = _state.load(std::memory_order_acquire);
    if (s != AlgorithmState::Idle)
        return false;
    if (!_algorithm->is_serializable() || data.empty())
        return false;
    _algorithm->deserialize_state(data);
    return true;
}

bool AlgorithmExecutor::is_serializable() const noexcept {
    return _algorithm && _algorithm->is_serializable();
}
