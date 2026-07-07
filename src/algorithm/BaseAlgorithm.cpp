#include "BaseAlgorithm.h"

#include "utils/SolutionFactory.h"
#include "algorithm/IAlgorithm.h"

#include <chrono>
#include <stdexcept>

void BaseAlgorithm::init(const Config &config) {
    if (_state == Running)
        return;
    _state = None;
    _config = config;
    _init(config);
    _state = Ready;
}
void BaseAlgorithm::run(const Input &input) {
    if (_state != Ready)
        return;
    _state = Running;
    _input = input;
    _run(input);
}
void BaseAlgorithm::stop() {
    if (_state != Running)
        return;
    _state = _stop() ? Finished : Ready;
}

BaseAlgorithm::State BaseAlgorithm::get_state() const noexcept { return _state; }
BaseAlgorithm::Output BaseAlgorithm::get_output() const {
    if (_state != State::Finished)
        return {.is_valid = false};
    return _output;
}

namespace Utils {

std::vector<EnchSolution> make_solution(const BaseAlgorithm::Input &input, const BaseAlgorithm::Output &output) {
    return SolutionFactory::create(
        input.platform,
        input.original_ench,
        input.target_item,
        input.available_items,
        AlgorithmOutput{
            .algorithm_name = output.algorithm_name,
            .algorithm_version = output.algorithm_version,
            .created_at = std::chrono::system_clock::now(),
            .computation_time = std::chrono::milliseconds(output.computation_time),
            .steps = output.steps,
            .is_valid = output.is_valid,
        }
    );
}

}; // namespace Utils
