#pragma once

namespace algorithm {

/// Algorithm executor state machine.
enum class AlgorithmState {
    Idle,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

} // namespace algorithm
