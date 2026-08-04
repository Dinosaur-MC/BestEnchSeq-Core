#pragma once

namespace algorithm {

/// Algorithm executor state machine.
enum class AlgorithmState {
    Idle,
    Running,
    Pausing, ///< pause() requested; the algorithm has not yet quiesced at wait_if_paused()
    Paused,
    Completed,
    Failed,
    Cancelled,
};

} // namespace algorithm
