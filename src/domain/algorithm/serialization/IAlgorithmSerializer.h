#pragma once
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/Checkpoint.h"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace algorithm {
class IAlgorithm;

class IAlgorithmSerializer {
public:
    virtual ~IAlgorithmSerializer() = default;

    virtual std::string_view algorithm_name() const noexcept = 0;
    virtual std::string_view algorithm_version() const noexcept = 0;

    std::vector<uint8_t> serialize(const IAlgorithm& algo, const AlgorithmInput& input) const;
    bool deserialize(IAlgorithm& algo, AlgorithmInput& out_input, std::span<const uint8_t> data) const;

protected:
    virtual std::vector<checkpoint::Section> _serialize_state(const IAlgorithm& algo) const = 0;
    virtual bool _deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const = 0;
};

} // namespace algorithm
