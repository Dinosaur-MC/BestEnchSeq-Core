#include "AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include "algorithm/serialization/IAlgorithmSerializer.h"
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

bool AlgorithmExecutor::_set_state(AlgorithmState new_state) noexcept {
    while (true) {
        auto prev = _state.load(std::memory_order_acquire);
        // Don't transition FROM terminal states
        if (prev == AlgorithmState::Cancelled || prev == AlgorithmState::Failed)
            return false;
        // Don't transition TO the same state (noop)
        if (prev == new_state)
            return false;
        if (_state.compare_exchange_weak(prev, new_state,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            _state_cv.notify_all();
            if (!_algo_name_cache.empty()) {
                DiagnosticsService::instance().push(
                    DiagEventKind::StateChange, _algo_name_cache, _task_id,
                    DiagnosticsEvent::StatePayload{prev, new_state});
            }
            return true;
        }
        // CAS failed — retry with updated prev
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
    if (!_set_state(AlgorithmState::Running))
        throw std::logic_error("executor already running or in terminal state");

    _task_id = _next_task_id.fetch_add(1, std::memory_order_relaxed);
    _algo_name_cache = std::string(_algorithm->name());
    _ctx = std::make_unique<ExecutionContext>(_task_id, _algo_name_cache.c_str());
    _start_time = std::chrono::steady_clock::now();

    // Warmup phase (synchronous): run a fast algorithm to tighten bound
    if (warmup)
        _run_warmup(input, *warmup);

    _algorithm_input = std::move(input);

    // Main phase (asynchronous): run the actual algorithm
    _worker.emplace([this]() mutable {
        try {
            _algorithm->execute(_algorithm_input, *_ctx);

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

void AlgorithmExecutor::start(const std::vector<uint8_t>& checkpoint) {
    if (checkpoint.empty())
        throw std::invalid_argument("empty checkpoint");

    auto* ser = _algorithm ? _algorithm->get_serializer() : nullptr;
    if (!ser)
        throw std::logic_error("algorithm does not support serialization");

    AlgorithmInput restored_input;
    bool ok = ser->deserialize(*_algorithm, restored_input, checkpoint);
    if (!ok)
        throw std::runtime_error("checkpoint deserialization failed");

    _algorithm_input = std::move(restored_input);

    if (!_set_state(AlgorithmState::Running))
        throw std::logic_error("executor already running or in terminal state");

    _task_id = _next_task_id.fetch_add(1, std::memory_order_relaxed);
    _algo_name_cache = std::string(_algorithm->name());
    _ctx = std::make_unique<ExecutionContext>(_task_id, _algo_name_cache.c_str());
    _start_time = std::chrono::steady_clock::now();

    _worker.emplace([this]() mutable {
        try {
            _algorithm->execute(_algorithm_input, *_ctx);

            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);
            _set_state(AlgorithmState::Completed);
        } catch (...) {
            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);
            _set_state(AlgorithmState::Failed);
        }
    });
}

void AlgorithmExecutor::pause() {
    if (_set_state(AlgorithmState::Paused))
        _ctx->pause();
}

void AlgorithmExecutor::resume() {
    if (_set_state(AlgorithmState::Running))
        _ctx->resume();
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
    auto* ser = _algorithm ? _algorithm->get_serializer() : nullptr;
    if (!ser)
        return {};
    return ser->serialize(*_algorithm, _algorithm_input);
}

bool AlgorithmExecutor::restore_state(const std::vector<uint8_t>& data) {
    auto s = _state.load(std::memory_order_acquire);
    if (s != AlgorithmState::Idle)
        return false;
    if (data.empty())
        return false;
    auto* ser = _algorithm ? _algorithm->get_serializer() : nullptr;
    if (!ser)
        return false;
    AlgorithmInput input;
    if (!ser->deserialize(*_algorithm, input, data))
        return false;
    _algorithm_input = std::move(input);
    return true;
}

bool AlgorithmExecutor::is_serializable() const noexcept {
    auto* ser = _algorithm ? _algorithm->get_serializer() : nullptr;
    return ser != nullptr;
}
