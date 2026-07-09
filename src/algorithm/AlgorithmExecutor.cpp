#include "AlgorithmExecutor.h"
#include "IAlgorithm.h"
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
            _ctx->report_state_change(prev, new_state);
            _state_cv.notify_all();
            return;
        }
    }
}

// ─── Lifecycle ───

void AlgorithmExecutor::start(
    std::vector<compact::Item> items,
    const compact::EnchReg& reg,
    std::vector<compact::Ench> target,
    const Equipment* out_eq)
{
    AlgorithmState expected = AlgorithmState::Idle;
    if (!_state.compare_exchange_strong(expected, AlgorithmState::Running))
        throw std::logic_error("executor already running");

    _out_equipment = out_eq;
    _start_time = std::chrono::steady_clock::now();
    _ctx->report_state_change(AlgorithmState::Idle, AlgorithmState::Running);

    _worker.emplace([this, items = std::move(items), &reg, target = std::move(target)]() mutable {
        try {
            _algorithm->execute(items, reg, target, *_ctx);

            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);

            if (_ctx->is_cancelled()) {
                _set_state(AlgorithmState::Cancelled);
            } else {
                _set_state(AlgorithmState::Completed);
            }
        } catch (...) {
            _computation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _start_time);
            _set_state(AlgorithmState::Failed);
        }
    });
}

void AlgorithmExecutor::start(
    std::vector<compact::Item> items,
    const compact::EnchReg& reg,
    std::vector<compact::Ench> target,
    const Equipment* out_eq,
    const std::vector<uint8_t>& previous_state)
{
    if (!previous_state.empty()) {
        _algorithm->deserialize_state(previous_state);
    }
    start(std::move(items), reg, std::move(target), out_eq);
}

void AlgorithmExecutor::pause() {
    AlgorithmState expected = AlgorithmState::Running;
    if (_state.compare_exchange_strong(expected, AlgorithmState::Paused))
        _ctx->pause();
}

void AlgorithmExecutor::resume() {
    AlgorithmState expected = AlgorithmState::Paused;
    if (_state.compare_exchange_strong(expected, AlgorithmState::Running))
        _ctx->resume();
}

void AlgorithmExecutor::cancel() {
    AlgorithmState prev = _state.exchange(AlgorithmState::Cancelled, std::memory_order_acq_rel);
    if (prev == AlgorithmState::Running || prev == AlgorithmState::Paused) {
        _ctx->cancel();
        _ctx->resume();
    }
    _state_cv.notify_all();
}

AlgorithmState AlgorithmExecutor::wait() {
    _join_worker();
    _ctx->dispatch_events();
    return _state.load(std::memory_order_acquire);
}

AlgorithmState AlgorithmExecutor::wait_for(std::chrono::milliseconds timeout) {
    if (!_worker)
        return _state.load(std::memory_order_acquire);

    if (!_worker->joinable())
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
        _ctx->dispatch_events();
    }
    return s;
}

// ─── State queries ───

AlgorithmState AlgorithmExecutor::state() const noexcept {
    return _state.load(std::memory_order_acquire);
}

double AlgorithmExecutor::progress() const noexcept {
    return _ctx ? _ctx->progress() : 0.0;
}

// ─── Observer ───

void AlgorithmExecutor::attach_observer(std::shared_ptr<AlgorithmObserver> observer) {
    if (_ctx) _ctx->attach_observer(std::move(observer));
}

void AlgorithmExecutor::detach_observer(std::shared_ptr<AlgorithmObserver> observer) {
    if (_ctx) _ctx->detach_observer(std::move(observer));
}

// ─── Result (convert compact steps → domain at output boundary) ───

AlgorithmOutput AlgorithmExecutor::output() const {
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Completed)
        return {.is_valid = false};

    AlgorithmOutput out;
    out.algorithm_name = std::string(_algorithm->name());
    out.algorithm_version = std::string(_algorithm->version());
    out.created_at = std::chrono::system_clock::now();
    out.computation_time = _computation_time;

    auto compact_steps = _ctx->get_accumulated_compact_steps();
    out.steps = std::move(compact_steps);

    out.is_valid = true;
    return out;
}

std::vector<uint8_t> AlgorithmExecutor::serialize_state() const {
    auto s = _state.load(std::memory_order_acquire);
    if (s == AlgorithmState::Idle)
        return {};
    if (!_algorithm->is_resumable())
        return {};
    return _algorithm->serialize_state();
}

bool AlgorithmExecutor::restore_state(const std::vector<uint8_t>& data) {
    if (_state.load(std::memory_order_acquire) != AlgorithmState::Idle)
        return false;
    if (!_algorithm->is_resumable())
        return false;
    if (data.empty())
        return false;
    _algorithm->deserialize_state(data);
    return true;
}

void AlgorithmExecutor::update_search_config(ExecutionContext::SearchConfig cfg) {
    if (_ctx) {
        _ctx->set_search_config(std::move(cfg));
    }
}
