#include "AlgorithmExecutor.h"
#include "algorithm/diagnostics/DiagnosticsService.h"
#include <stdexcept>
#include <utility>

// ─── Construction / Destruction ───

AlgorithmExecutor::AlgorithmExecutor(std::unique_ptr<IAlgorithm> algorithm)
    : _algorithm(std::move(algorithm))
    , _ctx(std::make_unique<ExecutionContext>())
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
            return;
        }
    }
}

void AlgorithmExecutor::_run_warmup(AlgorithmInput& input, IAlgorithm& warmup_algo) {
    ExecutionContext warmup_ctx;
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
    _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _start_time);

    auto atoms = _ctx->get_diagnostics(_computation_time.count());
    auto kvs = _ctx->consume_diagnostic_log();

    // Inject atomic counters into the KV log
    kvs.emplace_back("nodes_visited", std::to_string(atoms.nodes_visited));
    kvs.emplace_back("nodes_pruned",  std::to_string(atoms.nodes_pruned));
    kvs.emplace_back("steps_forged",  std::to_string(atoms.steps_forged));

    // Determine status from final state
    auto s = _state.load(std::memory_order_acquire);
    const char* status = "Complete";
    if (s == AlgorithmState::Cancelled)  status = "Cancelled";
    if (s == AlgorithmState::Failed)     status = "Failed";

    // Convert KV pairs to Writer::Entry
    std::vector<DiagnosticsWriter::Entry> entries;
    entries.reserve(kvs.size());
    for (auto& [k, v] : kvs)
        entries.push_back({std::move(k), std::move(v)});

    DiagnosticsService::instance().push(DiagnosticsEvent{
        .algorithm_name = std::string(_algorithm->name()),
        .entries        = std::move(entries),
        .status         = status,
        .timestamp      = std::chrono::system_clock::now(),
    });
}

// ─── Lifecycle ───

void AlgorithmExecutor::start(AlgorithmInput input,
                               std::unique_ptr<IAlgorithm> warmup) {
    // Warmup phase (synchronous): run a fast algorithm to tighten bound
    if (warmup)
        _run_warmup(input, *warmup);

    AlgorithmState expected = AlgorithmState::Idle;
    if (!_state.compare_exchange_strong(expected, AlgorithmState::Running))
        throw std::logic_error("executor already running");

    _start_time = std::chrono::steady_clock::now();

    _worker.emplace([this, input = std::move(input)]() mutable {
        try {
            _algorithm->execute(input, *_ctx);

            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);

            if (_ctx->is_cancelled())
                _set_state(AlgorithmState::Cancelled);
            else
                _set_state(AlgorithmState::Completed);
        } catch (...) {
            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);
            _set_state(AlgorithmState::Failed);
        }
    });
}

void AlgorithmExecutor::start(AlgorithmInput input, const std::vector<uint8_t>& previous_state) {
    if (!previous_state.empty())
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
    // Don't clobber Completed — results would be lost
    if (prev == AlgorithmState::Completed) {
        _state.store(AlgorithmState::Completed, std::memory_order_release);
        return;
    }
    if (prev == AlgorithmState::Running || prev == AlgorithmState::Paused) {
        _ctx->cancel();
        _ctx->resume();
    }
    _state_cv.notify_all();
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
    if (s == AlgorithmState::Idle || !_algorithm->is_resumable())
        return {};
    return _algorithm->serialize_state();
}

bool AlgorithmExecutor::restore_state(const std::vector<uint8_t>& data) {
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Idle)
        return false;
    if (!_algorithm->is_resumable() || data.empty())
        return false;
    _algorithm->deserialize_state(data);
    return true;
}
